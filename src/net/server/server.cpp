#include "kvstore/net/server/server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

#include "kvstore/net/server/connection.hpp"
#include "kvstore/net/server/event_loop.hpp"
#include "kvstore/net/server/protocol_handler.hpp"
#include "kvstore/util/logger.hpp"
#include "kvstore/util/types.hpp"

namespace kvstore::net::server {

namespace util = kvstore::util;

namespace {

// install once at startup to ignore SIGPIPE
/*
    when you write to socket that the other end has closed, OS sends your process a SIGPIPE signal
   -> default behavior: terminate the process 2 ways to handle:
    1. signal(SIGPIPE, SIG_IGN) -> ignore globally, send() returns -1 with errno = EPIPE
    2. MSG_NOSIGNAL flag on each send() call - same effect, per call
    - we do both for safety
*/
struct SigpipeIgnorer {
    SigpipeIgnorer() {
        signal(SIGPIPE, SIG_IGN);
    }
};

static SigpipeIgnorer sigpipe_ignorer;

}  // namespace

// Server implementation using single-threaded event loop model (epoll/kqueue).
//
// Threading Model:
// - One event thread runs the event loop and handles ALL client I/O
// - One cleanup thread periodically removes expired keys (if enabled)
// - The main thread can call start()/stop() from outside
//
// This single-threaded I/O model has important benefits:
// - No locks needed for connection map or individual connections
// - No race conditions when closing connections (close always happens from event thread)
// - Simpler reasoning about state transitions
// - Callbacks for a given fd are never called concurrently
//
// The trade-off is that one slow client can block others. For a KV store with
// small requests this is acceptable. For production with large payloads,
// consider thread-per-connection or thread pool models.
class Server::Impl {
   public:
    Impl(core::IStore& store, const ServerOptions& options)
        : store_(store),
          options_(options),
          limits_{options.max_message_size, options.max_key_size, options.max_value_size,
                  options.max_line_size} {}

    ~Impl() {
        stop();
    }
    /*
        note: compiler auto generates copy/move only if all members support it.
        std::mutex - not cpyable not moveable
        std::thread - not copyable, moveable
        std::atomic - not copyable, movable
        std::vector<std::thread> - not copyabl, moveable
        - because of mutex, compiler auto deletes both for Impl
        - but also we dont care because we never copy or move Impl direcly, only move
       unique_ptr<Impl>
    */
    void start() {
        /*
            1. create socket with protocols (IPv4 & TCP)
            2. set socket options
            3. set up address struct
            4. bind address&port to socket
            5. mark for listen
            6. spawn thread to accept connections
        */
        if (running_)
            return;

        // AF_INET = IPv4, SOCK_STREAM = TCP
        // socket sctor returns a file descriptor (integer handle)
        // negative means error
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            throw std::runtime_error("failed to create socket: " + std::string(strerror(errno)));
            // note: errno is global (thread-local) variable set by syscalls on failure.
            // errno only vaid immediately after failed call. any subsequent call may overwrite
        }

        // SO_REUSEADDR lets us rebind to port immediately after restart. without it you get
        // "address already in use" for ~30s after stopping
        /*
            SOL_SOCKET speicifed level at which option is defined:
            SOL_SOCKET (generic socket layer) : SO_REUSEADDR, SO_KEEPALIVE, SO_RCVBUF ...
            IPPROTO_TCP (TCP protocol layer) : TCP_NODELAY, TCP_KEEPIDLE ...
            IPPROTO_IP (IP protocol layer) : IP_TTL, IP_MULTICAST_IF ...
        */
        int opt = 1;

        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            close(fd);
            throw std::runtime_error("failed to set SO_REUSEADDR: " + std::string(strerror(errno)));
        }

        // set up address struct. htons conver port to network byte order (big-endian)
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(options_.port);

        // inet_pton (inet presentation to network) converts string IP("127.0.0.1") to binary format
        // & store in addr.sin_addr
        if (inet_pton(AF_INET, options_.host.c_str(), &addr.sin_addr) <= 0) {
            close(fd);
            throw std::runtime_error("Invalid address: " + options_.host);
        }

        // bind associated socket with specific address and port
        // before bind, socket exists but has no address -> OS doesnt know where to route incoming
        // packets after bind, socket bound to 127.0.0.1:6379. OS routes packets for that addr/port
        // to this socket
        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(fd);
            throw std::runtime_error("failed to bind to port " + std::to_string(options_.port) +
                                     ": " + std::string(strerror(errno)));
        }

        // query actual bound port (for options_.port is 0)
        sockaddr_in bound_addr{};
        socklen_t bound_len = sizeof(bound_addr);
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&bound_addr), &bound_len) == 0) {
            actual_port_ = ntohs(bound_addr.sin_port);
        } else {
            actual_port_ = options_.port;
        }

        // mark socket as listening - SOMAXCONN is max backlog of pending connections
        if (listen(fd, SOMAXCONN) < 0) {
            close(fd);
            throw std::runtime_error("failed to listen: " + std::string(strerror(errno)));
        }

        // make server socket non-blocking
        if (!Connection::set_nonblocking(fd)) {
            close(fd);
            throw std::runtime_error("failed to set server socket non-blocking");
        }

        // set running flag
        server_fd_.store(fd);
        running_ = true;

        // run event loop in a thread instead of accept loop
        event_thread_ = std::thread(&Impl::event_loop_run, this);

        // start cleanup thread if enabled
        if (options_.cleanup_interval_ms > 0) {
            cleanup_thread_ = std::thread(&Impl::cleanup_loop, this);
        }

        LOG_INFO("Server started on " + options_.host + ":" + std::to_string(options_.port));
    }

    void stop() {
        // exchange sets the value as argument and returns old value
        // if already stopped, return early.
        if (!running_.exchange(false))
            return;

        LOG_INFO("Server stopping...");

        // stop event loop (thread-safe)
        loop_.stop();

        int fd = server_fd_.exchange(-1);
        // shutdown stops reads and writes, unblocking any threads stuck in accept()
        // close releases fd
        if (fd >= 0) {
            shutdown(fd, SHUT_RDWR);  // can fail, but we ignore
            close(fd);                // can fail, but we ignroe
        }

        // wait for accept thread to finish
        if (event_thread_.joinable()) {
            event_thread_.join();  // noexcept
        }

        // wait for cleanup thread to finish
        if (cleanup_thread_.joinable()) {
            cleanup_thread_.join();  // noexcept
        }

        // close all client connections
        connections_.clear();

        LOG_INFO("Server stopped");
    }

    [[nodiscard]] bool running() const noexcept {
        return running_;
    }

    [[nodiscard]] uint16_t port() const noexcept {
        return actual_port_;
    }

   private:
    void event_loop_run() {
        // add server socket to watch for incoming connecitons
        loop_.add(server_fd_.load(), kEventRead, [this](int, uint32_t) { handle_accept(); });
        loop_.run();
    }

    void handle_accept() {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd =
            accept(server_fd_.load(), reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG_ERROR("Accept failed: " + std::string(strerror(errno)));
            }
            return;
        }

        // check connection limit
        if (connections_.size() >= options_.max_connections) {
            LOG_WARN("Max connections reached, rejecting client");
            // Send error response before closing (text format - works for text clients,
            // binary clients will see garbled text but won't hang waiting)
            const char* error_msg = "ERROR max connections reached\n";
            // Best-effort send - ignore failure since we're closing anyway
            (void)send(client_fd, error_msg, strlen(error_msg), MSG_NOSIGNAL);
            close(client_fd);
            return;
        }

        // make non-blocking and create connection
        if (!Connection::set_nonblocking(client_fd)) {
            close(client_fd);
            LOG_ERROR("Failed to set client socket non-blocking");
            return;
        }
        std::unique_ptr<Connection> conn;
        try {
            conn = std::make_unique<Connection>(client_fd, limits_);
        } catch (...) {
            // Note: Connection constructor initializes FdGuard first, so fd is already
            // owned and will be closed by FdGuard destructor. Don't close here.
            LOG_ERROR("Failed to create connection");
            return;
        }
        connections_[client_fd] = std::move(conn);

        LOG_DEBUG("Client connected, fd=" + std::to_string(client_fd));

        // add to event loop, watch for reads
        loop_.add(client_fd, kEventRead,
                  [this, client_fd](int, uint32_t events) { handle_client(client_fd, events); });
    }

    /*
        note: in handle client we add kEventWrite only after processing a request.
        if socket was already writable we wont know until next epoll_wait() call
        latency is at most 1 poll iteration (100ms timeout) this is okay for most cases

        for high throughput scenarios you could: try writing imeediately after queueing response
        and only watch kEventWrite if theres still data left
        or use edge-triggered mode (EPOLLET) which requires more careful handling
    */

    void handle_client(int client_fd, uint32_t events) {
        auto it = connections_.find(client_fd);
        if (it == connections_.end())
            return;

        auto& conn = it->second;

        // errors/hangups
        if (events & (kEventError | kEventHangup)) {
            if (events & kEventError) {
                LOG_DEBUG("Client error, fd=" + std::to_string(client_fd));
            } else {
                LOG_DEBUG("Client hangup, fd=" + std::to_string(client_fd));
            }
            close_client(client_fd);
            return;
        }

        // handle readable (incoming data)
        if (events & kEventRead) {
            auto result = conn->do_read();
            if (result == IoResult::Closed || result == IoResult::Error) {
                close_client(client_fd);
                return;
            }

            // try to parse & process requests
            while (auto req = conn->try_parse_request()) {
                Response response;
                try {
                    response = process_request(*req);
                } catch (const std::exception& e) {
                    response = Response::error(std::string("internal error: ") + e.what());
                }

                conn->queue_response(response);

                if (response.close_connection) {
                    // flush response then close
                    conn->do_write();
                    close_client(client_fd);
                    return;
                }
            }

            // check if parsing failed due to limit violation
            if (conn->has_protocol_error()) {
                LOG_WARN("Protocol limit exceeded for fd=" + std::to_string(client_fd) + ": " +
                         conn->protocol_error());
                conn->queue_response(Response::error(conn->protocol_error()));
                conn->do_write();
                close_client(client_fd);
                return;
            }

            // if we have data to send, watch for writability
            if (conn->has_pending_write()) {
                loop_.modify(client_fd, kEventRead | kEventWrite);
            }
        }

        // handle writable (can send data)
        if (events & kEventWrite) {
            auto result = conn->do_write();
            if (result == IoResult::Error) {
                close_client(client_fd);
                return;
            }

            // if nothing left to write, stop watching kEventWrite
            if (!conn->has_pending_write()) {
                loop_.modify(client_fd, kEventRead);
            }
        }
    }

    // Close a client connection and clean up resources.
    // Safe to call without synchronization because:
    // 1. Only called from event loop thread (via handle_client callback)
    // 2. EventLoop defers callback removal until after current callback returns
    // 3. Connection destructor closes the fd via FdGuard
    void close_client(int client_fd) {
        LOG_DEBUG("Client disconnected, fd=" + std::to_string(client_fd));
        loop_.remove(client_fd);
        connections_.erase(client_fd);
    }

    Response process_request(const Request& req) {
        switch (req.command) {
            case Command::Get: {
                if (req.key.empty())
                    return Response::error("usage: GET key");
                auto result = store_.get(req.key);
                if (result.has_value())
                    return Response::ok(*result);
                return Response::not_found();
            }
            case Command::Put: {
                if (req.key.empty())
                    return Response::error("usage: PUT key value");
                store_.put(req.key, req.value);
                return Response::ok();
            }
            case Command::PutEx: {
                if (req.key.empty())
                    return Response::error("usage: PUTEX key ms value");
                store_.put(req.key, req.value, util::Duration(req.ttl_ms));
                return Response::ok();
            }
            case Command::Del: {
                if (req.key.empty())
                    return Response::error("usage: DEL key");
                if (store_.remove(req.key))
                    return Response::ok();
                return Response::not_found();
            }
            case Command::Exists: {
                if (req.key.empty())
                    return Response::error("usage: EXISTS key");
                return Response::ok(store_.contains(req.key) ? "1" : "0");
            }
            case Command::Size:
                return Response::ok(std::to_string(store_.size()));
            case Command::Clear:
                store_.clear();
                return Response::ok();
            case Command::Ping:
                return Response::ok("PONG");
            case Command::Quit:
                return Response::bye();
            case Command::Unknown:
            default:
                return Response::error("unknown command");
        }
    }

    void cleanup_loop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options_.cleanup_interval_ms));
            if (running_) {
                try {
                    store_.cleanup_expired();
                } catch (const std::exception& e) {
                    LOG_ERROR("Cleanup error: " + std::string(e.what()));
                } catch (...) {
                    LOG_ERROR("Cleanup error: unknown exception");
                }
            }
        }
    }
    core::IStore& store_;
    ServerOptions options_;
    ProtocolLimits limits_;

    uint16_t actual_port_{0};
    std::atomic<int> server_fd_{-1};
    // server_fd_ needs to be atomic: read by event thread while main thread can call stop() -
    // concurrent access
    std::atomic<bool> running_{false};

    // thread-per-client model replaced with EventLoop + connections map
    EventLoop loop_;
    std::unordered_map<int, std::unique_ptr<Connection>> connections_;
    std::thread cleanup_thread_;
    std::thread event_thread_;  // single thread rns event loop
};

// PIMPL INTERFACE -------------------------------------------------------------------------------
Server::Server(core::IStore& store, const ServerOptions& options)
    : impl_(std::make_unique<Impl>(store, options)) {}
Server::~Server() = default;
Server::Server(Server&&) noexcept = default;
Server& Server::operator=(Server&&) noexcept = default;
void Server::start() {
    impl_->start();
}
void Server::stop() {
    impl_->stop();
}
bool Server::running() const noexcept {
    return impl_->running();
}
uint16_t Server::port() const noexcept {
    return impl_->port();
}

}  // namespace kvstore::net::server