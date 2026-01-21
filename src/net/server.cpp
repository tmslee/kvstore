#include "kvstore/net/server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
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

namespace kvstore::net {

class Server::Impl {
   public:
    Impl(core::Store& store, const ServerOptions& options) : store_(store), options_(options) {}

    ~Impl() {
        // destructor should NOT throw.
        // but stop() CAN throw -> refer to the method implementation
        // here we log and swallow
        try {
            stop();
        } catch (const std::exception& e) {
            std::cerr << "Server::~Server: stop() failed: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "Server::~Server: stop() failed with unkown error\n";
        }
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

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        // AF_INET = IPv4, SOCK_STREAM = TCP
        // socket sctor returns a file descriptor (integer handle)
        // negative means error
        if (fd < 0) {
            throw std::runtime_error("failed to create socket");
        }

        int opt = 1;
        // SO_REUSEADDR lets us rebind to port immediately after restart. without it you get
        // "address already in use" for ~30s after stopping
        /*
            SOL_SOCKET speicifed level at which option is defined:
            SOL_SOCKET (generic socket layer) : SO_REUSEADDR, SO_KEEPALIVE, SO_RCVBUF ...
            IPPROTO_TCP (TCP protocol layer) : TCP_NODELAY, TCP_KEEPIDLE ...
            IPPROTO_IP (IP protocol layer) : IP_TTL, IP_MULTICAST_IF ...
        */
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            close(fd);
            throw std::runtime_error("failed to set socket options");
        }

        // set up address struct. htons conver port to network byte order (big-endian)
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(options_.port);

        // inet_pton (inet presentation to network) converts string IP("127.0.0.1") to binary format
        // & store in addr.sin_addr
        if (inet_pton(AF_INET, options_.host.c_str(), &addr.sin_addr) <= 0) {
            close(fd);
            throw std::runtime_error("invalid address: " + options_.host);
        }

        // bind associated socket with specific address and port
        // before bind, socket exists but has no address -> OS doesnt know where to route incoming
        // packets after bind, socket bound to 127.0.0.1:6379. OS routes packets for that addr/port
        // to this socket
        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(fd);
            throw std::runtime_error("failed to bind to port" + std::to_string(options_.port));
        }

        // mark socket as listening - SOMAXCONN is max backlog of pending connections
        if (listen(fd, SOMAXCONN) < 0) {
            close(fd);
            throw std::runtime_error("failed to listen");
        }

        // set running flag, spawn thread to accept connections
        server_fd_.store(fd);
        running_ = true;
        accept_thread_ = std::thread(&Impl::accept_loop, this);
    }

    void stop() {
        // exchange sets the value as argument and returns old value
        // if already stopped, return early.
        if (!running_.exchange(false)) {
            return;
        }

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
        for (auto& t : client_threads_) {
            if (t.joinable()) {
                t.join();  // noexcept
            }
        }
        client_threads_.clear();
        // std::lock_guard ctor can technically throw. rare but possible.
    }

    [[nodiscard]] bool running() const noexcept {
        return running_;
    }

    [[nodiscard]] uint16_t port() const noexcept {
        return options_.port;
    }

   private:
    void accept_loop() {
        while (running_) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);

            int fd = server_fd_.load();
            if (fd < 0) {
                break;
            }

            int client_fd =
                accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_fd < 0) {
                if (running_) {  // retry if we're still running
                    continue;
                }
                break;  // exit if we're done
            }
            {
                std::lock_guard lock(clients_mutex_);
                client_threads_.emplace_back(&Impl::handle_client, this, client_fd);
            }
        }
    }

    void handle_client(int client_fd) {
        std::string buffer;
        char chunk[1024];

        while (running_) {
            ssize_t bytes_read = recv(client_fd, chunk, sizeof(chunk) - 1, 0);
            // 0 = client closed connection, negative = error.
            if (bytes_read <= 0) {
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
                CommandResult result = process_command(line);
                std::string response = Protocol::serialize(result);

                // send response. if fail, close and exit.
                if (send(client_fd, response.c_str(), response.size(), 0) < 0) {
                    close(client_fd);
                    return;
                }

                if (result.close_connection) {
                    close(client_fd);
                    return;
                }
            }
        }
        close(client_fd);
    }

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

    core::Store& store_;
    ServerOptions options_;

    std::atomic<int> server_fd_ = -1;
    // server_fd_ needs to be atomic bc it gets written by main thread in stop()
    // while its read in accept_loop() by another thread.
    std::atomic<bool> running_ = false;

    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;
    std::mutex clients_mutex_;
};

// PIMPL INTERFACE -------------------------------------------------------------------------------
Server::Server(core::Store& store, const ServerOptions& options)
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