#include "kvstore/net/server/server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

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
    - we do both for afety
*/
struct SigpipeIgnorer {
    SigpipeIgnorer() {
        signal(SIGPIPE, SIG_IGN);
    }
};

static SigpipeIgnorer sigpipe_ignorer;

}  // namespace

class Server::Impl {
   public:
    Impl(core::IStore& store, const ServerOptions& options) : store_(store), options_(options) {}

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
        if (running_) {
            return;
        }
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

        // set running flag, spawn thread to accept connections
        server_fd_.store(fd);
        running_ = true;
        accept_thread_ = std::thread(&Impl::accept_loop, this);

        LOG_INFO("Server started on " + options_.host + ":" + std::to_string(options_.port));
    }

    void stop() {
        // exchange sets the value as argument and returns old value
        // if already stopped, return early.
        if (!running_.exchange(false)) {
            return;
        }

        LOG_INFO("Server stopping...");

        int fd = server_fd_.exchange(-1);
        // shutdown stops reads and writes, unblocking any threads stuck in accept()
        // close releases fd
        if (fd >= 0) {
            shutdown(fd, SHUT_RDWR);  // can fail, but we ignore
            close(fd);                // can fail, but we ignroe
        }

        // wait for accept thread to finish
        if (accept_thread_.joinable()) {
            accept_thread_.join();  // noexcept
        }

        // wait for all client handler threds to finish, then clear vector.
        std::lock_guard lock(clients_mutex_);  // can throw std::system_error
        for (auto& info : clients_) {
            if (info->thread.joinable()) {
                info->thread.join();  // noexcept
            }
        }
        clients_.clear();
        // std::lock_guard ctor can technically throw. rare but possible.

        LOG_INFO("Server stopped");
    }

    [[nodiscard]] bool running() const noexcept {
        return running_;
    }

    [[nodiscard]] uint16_t port() const noexcept {
        return actual_port_;
    }

   private:
    struct ClientInfo {
        std::thread thread;
        std::atomic<bool> finished{false};
    };

    void accept_loop() {
        while (running_) {
            // clean up finished client threads periodically
            cleanup_finished_clients();

            // check connection limit
            {
                std::lock_guard lock(clients_mutex_);
                if (clients_.size() >= options_.max_connections) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
            }

            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);

            int fd = server_fd_.load();
            if (fd < 0) {
                break;
            }

            int client_fd =
                accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

            if (client_fd < 0) {
                if (running_ && errno != EINTR) {  // retry if we're still running
                    LOG_ERROR("Accept failed: " + std::string(strerror(errno)));
                }
                continue;
            }

            // set client socket timeout
            if (options_.client_timeout_seconds > 0) {
                struct timeval tv;
                tv.tv_sec = options_.client_timeout_seconds;
                tv.tv_usec = 0;
                setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            }

            LOG_DEBUG("Client connected, fd=" + std::to_string(client_fd));

            {
                std::lock_guard lock(clients_mutex_);
                auto info = std::make_unique<ClientInfo>();
                auto* info_ptr = info.get();
                /*
                    IMPORTANT NOTE:
                    - std::thread copies arguments by default!
                    - if we pass ClientInfo by value, bceause it contains std::thread (not copyable)
                   this will not work
                    - if we pass ClientInfo by reference, it will work but we need std::ref()
                        i.e. std::thread(&Impl::handle_client, this, client_fd,
                   std::ref(*info_ptr));
                        - this is fine but gotta rmb + error msgs are confusing
                    - if we just pass by pointer, this is simple & explicit
                */

                // we pass 'this' to thread because handle_client is a member function.
                // member functions have an implicit this paramter:
                //      - void handle_client(Impl* this, int fd, ClientInfo* info);
                info->thread = std::thread(&Impl::handle_client, this, client_fd, info_ptr);
                clients_.push_back(std::move(info));
            }
        }
    }

    void cleanup_finished_clients() {
        std::lock_guard lock(clients_mutex_);
        auto it = clients_.begin();
        while (it != clients_.end()) {
            if ((*it)->finished.load()) {
                if ((*it)->thread.joinable()) {
                    (*it)->thread.join();
                }
                it = clients_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void handle_client(int client_fd, ClientInfo* info) {
        try {
            auto handler = create_protocol_handler(client_fd, options_.binary_only);
            if (!handler) {
                close(client_fd);
                info->finished.store(true);
                return;
            }
            // note: running_ is atomic<bool>. when you use atomic in a boolean context, it
            // implicitly calls load()
            while (running_) {
                auto request = handler->read_request(client_fd);
                if (!request) {
                    break;
                }
                Response response;
                try {
                    response = process_request(*request);
                } catch (const std::exception& e) {
                    response = Response::error(std::string("internal error: ") + e.what());
                } catch (...) {
                    response = Response::error("internal error");
                }

                if (!handler->write_response(client_fd, response) || response.close_connection) {
                    break;
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Client handle error: " + std::string(e.what()));
        } catch (...) {
            LOG_ERROR("Client handler unkown error");
        }

        close(client_fd);
        info->finished.store(true);
        LOG_DEBUG("Client disconnected, fd=" + std::to_string(client_fd));
    }

    Response process_request(const Request& req) {
        switch (req.command) {
            case Command::Get: {
                if (req.key.empty()) {
                    return Response::error("usage: GET key");
                }
                auto result = store_.get(req.key);
                if (result.has_value()) {
                    return Response::ok(*result);
                }
                return Response::not_found();
            }

            case Command::Put: {
                if (req.key.empty()) {
                    return Response::error("usage: PUT key value");
                }
                store_.put(req.key, req.value);
                return Response::ok();
            }

            case Command::PutEx: {
                if (req.key.empty()) {
                    return Response::error("usage: PUTEX key ms value");
                }
                store_.put(req.key, req.value, util::Duration(req.ttl_ms));
                return Response::ok();
            }

            case Command::Del: {
                if (req.key.empty()) {
                    return Response::error("usage: DEL key");
                }
                if (store_.remove(req.key)) {
                    return Response::ok();
                }
                return Response::not_found();
            }

            case Command::Exists: {
                if (req.key.empty()) {
                    return Response::error("usage: EXISTS key");
                }
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

    core::IStore& store_;
    ServerOptions options_;

    uint16_t actual_port_{0};

    std::atomic<int> server_fd_{-1};
    // server_fd_ needs to be atomic bc it gets written by main thread in stop()
    // while its read in accept_loop() by another thread.
    std::atomic<bool> running_{false};

    std::thread accept_thread_;

    // important note: we use std::vector<std::unique_ptr<>> bceause vector reallocation will
    // invalidate address of stored objects. using a pointer alleviates this - heap objects have
    // stable addresses.
    std::vector<std::unique_ptr<ClientInfo>> clients_;
    std::mutex clients_mutex_;
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