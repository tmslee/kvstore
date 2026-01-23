#include "kvstore/net/server.hpp"

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

#include "kvstore/net/protocol.hpp"
#include "kvstore/util/types.hpp"
#include "kvstore/util/logger.hpp"

namespace kvstore::net {

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
        return options_.port;
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

            LOG_DEBUG("Client connected, fd=" + std::to_string(client_fd));

            // set client socket timeout
            if (options_.client_timeout_seconds > 0) {
                struct timeval tv;
                tv.tv_sec = options_.client_timeout_seconds;
                tv.tv_usec = 0;
                setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            }
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
            std::string buffer;
            char chunk[1024];

            // note: running_ is atomic<bool>. when you use atomic in a boolean context, it
            // implicitly calls load()
            while (running_) {
                ssize_t bytes_read = recv(client_fd, chunk, sizeof(chunk) - 1, 0);

                // negative bytes_read = error.
                // 0 bytes_read = disconnect
                if (bytes_read < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // timeout - client idle too long
                        break;
                    }
                    if (errno == EINTR) {
                        continue;  // retry
                    }
                    // other error
                    break;
                }
                if (bytes_read == 0) {
                    break;
                }

                // null-terminate the chunk @ end of bytes read and append to buffer
                // chunk is a C-string: without null term it would read past valid data into garbage
                // memory. alternatively, buffer.append(chunk, bytes_read);
                /*
                    IMPORTANT NOTE: network APIs are C functions. we use c-strings (null terminated
                character) because:
                        - direct compatibility with syscalls
                        - no heap allocation for small buffers
                        - explicit control over memory layout
                */
                chunk[bytes_read] = '\0';
                buffer += chunk;

                size_t pos;
                // while we have complete lines in our buffer, extract the line from buffer (1 full
                // command) and process it.
                while ((pos = buffer.find('\n')) != std::string::npos) {
                    std::string line = buffer.substr(0, pos);
                    buffer.erase(0, pos + 1);

                    // append newline so client can recognize end of response
                    CommandResult result;
                    // catch any exceptions and return as Protocol::error
                    try {
                        result = process_command(line);
                    } catch (const std::exception& e) {
                        result = Protocol::error(std::string("internal error: ") + e.what());
                    } catch (...) {
                        result = Protocol::error("internal error");
                    }

                    std::string response = Protocol::serialize(result);

                    // send failed
                    if (!send_all(client_fd, response)) {
                        close(client_fd);
                        info->finished.store(true);
                        return;
                    }
                    // client requested disconnect
                    if (result.close_connection) {
                        close(client_fd);
                        info->finished.store(true);
                        return;
                    }
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

    bool send_all(int fd, const std::string& data) {
        // loop until bytes sent for partial sends
        size_t total_sent = 0;
        while (total_sent < data.size()) {
            ssize_t sent =
                send(fd, data.c_str() + total_sent, data.size() - total_sent, MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EINTR) {
                    continue;  // retry
                }
                return false;
            }
            if (sent == 0) {
                return false;
            }
            total_sent += sent;
        }
        return true;
    }

    /*
        note: we use Protocol::error() when client did something wrong. we tell them
        std::cerr is used for internal issues. we log for operations
    */
    CommandResult process_command(const std::string& line) {
        ParsedCommand cmd = Protocol::parse(line);
        if (cmd.command.empty()) {
            return Protocol::error("empty command");
        }

        if (cmd.command == "GET") {
            if (cmd.args.size() != 1) {
                return Protocol::error("usage: GET key");
            }
            auto result = store_.get(cmd.args[0]);
            if (result.has_value()) {
                return Protocol::ok(*result);
            }
            return Protocol::not_found();

        } else if (cmd.command == "PUT" || cmd.command == "SET") {
            if (cmd.args.size() < 2) {
                return Protocol::error("usage: PUT key value");
            }
            std::string value;
            for (size_t i = 1; i < cmd.args.size(); ++i) {
                if (i > 1)
                    value += " ";
                value += cmd.args[i];
            }
            store_.put(cmd.args[0], value);
            return Protocol::ok();

        } else if (cmd.command == "PUTEX" || cmd.command == "SETEX") {
            if (cmd.args.size() < 3) {
                return Protocol::error("usage: PUTEX key milliseconds value");
            }
            auto ttl = util::Duration(std::stoll(cmd.args[1]));
            std::string value;
            for (size_t i = 2; i < cmd.args.size(); ++i) {
                if (i > 2)
                    value += " ";
                value += cmd.args[i];
            }
            store_.put(cmd.args[0], value, ttl);
            return Protocol::ok();

        } else if (cmd.command == "DEL" || cmd.command == "DELETE" || cmd.command == "REMOVE") {
            if (cmd.args.size() != 1) {
                return Protocol::error("usage: DEL key");
            }
            if (store_.remove(cmd.args[0])) {
                return Protocol::ok();
            }
            return Protocol::not_found();

        } else if (cmd.command == "EXISTS" || cmd.command == "CONTAINS") {
            if (cmd.args.size() != 1) {
                return Protocol::error("usage: EXISTS key");
            }
            if (store_.contains(cmd.args[0])) {
                return Protocol::ok("1");
            }
            return Protocol::ok("0");

        } else if (cmd.command == "SIZE" || cmd.command == "COUNT") {
            return Protocol::ok(std::to_string(store_.size()));

        } else if (cmd.command == "CLEAR") {
            store_.clear();
            return Protocol::ok();

        } else if (cmd.command == "PING") {
            return Protocol::ok("PONG");

        } else if (cmd.command == "QUIT" || cmd.command == "EXIT") {
            return Protocol::bye();
        }

        return Protocol::error("unkown command: " + cmd.command);
    }

    core::IStore& store_;
    ServerOptions options_;

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

}  // namespace kvstore::net