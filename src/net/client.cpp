#include "kvstore/net/client.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stdexcept>

namespace kvstore::net {

namespace util = kvstore::util;

class Client::Impl {
public:
    explicit Impl(const ClientOptions& options) : options_(options) {}

    ~Impl() {
        disconnect();
    }

    void connect() {
        if (socket_fd_ >= 0) {
            return;
        }

        socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);

        if (socket_fd_ < 0) {
            throw std::runtime_error("failed to create socket");
        }

        if (options_.timeout_seconds > 0) {
            struct timeval tv;
            tv.tv_sec = options_.timeout_seconds;
            tv.tv_usec = 0;
            setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(options_.port);

        if (inet_pton(AF_INET, options_.host.c_str(), &addr.sin_addr) <= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
            throw std::runtime_error("Invalid address: " + options_.host);
        }

        if (::connect(socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(socket_fd_);
            socket_fd_ = -1;
            throw std::runtime_error("failed to connect to " + options_.host + ":" +
                                    std::to_string(options_.port));
        }
    }

    void disconnect() {
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
    }

    [[nodiscard]] bool connected() const noexcept {
        return socket_fd_ >= 0;
    }

    /*
        note: we throw in user operations because errors are rare and unrecoverable (network down = cant
    continue anyway)
    */
    void put(std::string_view key, std::string_view value) {
        std::string cmd = "PUT " + std::string(key) + " " + std::string(value);
        std::string response = send_command(cmd);

        if (response.substr(0, 2) != "OK") {
            throw std::runtime_error("PUT failed: " + response);
        }
    }

    void put(std::string_view key, std::string_view value, util::Duration ttl) {
        std::string cmd = "PUTEX " + std::string(key) + " " + 
                      std::to_string(ttl.count()) + " " + std::string(value);
        std::string response = send_command(cmd);

        if (response.substr(0, 2) != "OK") {
            throw std::runtime_error("PUT failed: " + response);
        }
    }

    [[nodiscard]] std::optional<std::string> get(std::string_view key) {
        std::string cmd = "GET " + std::string(key);
        std::string response = send_command(cmd);

        if (response == "NOT_FOUND") {
            return std::nullopt;
        }
        if (response.substr(0, 3) == "OK ") {
            return response.substr(3);
        }

        throw std::runtime_error("GET failed: " + response);
    }

    [[nodiscard]] bool remove(std::string_view key) {
        std::string cmd = "DEL " + std::string(key);
        std::string response = send_command(cmd);

        if (response == "OK") {
            return true;
        }
        if (response == "NOT_FOUND") {
            return false;
        }

        throw std::runtime_error("DEL failed: " + response);
    }

    [[nodiscard]] bool contains(std::string_view key) {
        std::string cmd = "EXISTS " + std::string(key);
        std::string response = send_command(cmd);

        if (response == "OK 1") {
            return true;
        }
        if (response == "OK 0") {
            return false;
        }

        throw std::runtime_error("EXISTS failed: " + response);
    }

    [[nodiscard]] std::size_t size() {
        std::string response = send_command("SIZE");

        if (response.substr(0, 3) == "OK ") {
            return std::stoull(response.substr(3));
        }

        throw std::runtime_error("SIZE failed: " + response);
    }

    void clear() {
        std::string response = send_command("CLEAR");

        if (response != "OK") {
            throw std::runtime_error("CLEAR failed: " + response);
        }
    }

    [[nodiscard]] bool ping() {
        try {
            std::string response = send_command("PING");
            return response == "OK PONG";
        } catch (...) {
            return false;
        }
    }

private:
    std::string send_command(const std::string& command) {
        if (socket_fd_ < 0) {
            throw std::runtime_error("not connected");
        }

        std::string msg = command + "\n";
        size_t total_sent = 0;

        // we lop here until all bytes are sent
        while (total_sent < msg.size()) {
            // ssize_t : signed size type
            ssize_t sent = send(socket_fd_, msg.c_str() + total_sent, msg.size() - total_sent, 0);
            if (sent <= 0) {
                disconnect();
                throw std::runtime_error("failed to send command");
            }
            total_sent += sent;
        }
        return read_response();
    }

    std::string read_response() {
        std::string response;
        char c;

        while (true) {
            ssize_t n = recv(socket_fd_, &c, 1, 0);
            if (n <= 0) {
                disconnect();
                throw std::runtime_error("failed to read response");
            }
            if (c == '\n') {
                break;
            }
            response += c;
        }
        return response;
    }

    ClientOptions options_;
    int socket_fd_ = -1;
};

//PIMPL INTERFACE ------------------------------------------------------------------------
Client::Client(const ClientOptions& options)
    : impl_(std::make_unique<Impl>(options)) {}
Client::~Client() = default;
Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;
void Client::connect() {
    impl_->connect();
}
void Client::disconnect() {
    impl_->disconnect();
}
bool Client::connected() const noexcept {
    return impl_->connected();
}
void Client::put(std::string_view key, std::string_view value) {
    impl_->put(key, value);
}
void Client::put(std::string_view key, std::string_view value, util::Duration ttl) {
    impl_->put(key, value, ttl);
}
std::optional<std::string> Client::get(std::string_view key) {
    return impl_->get(key);
}
bool Client::remove(std::string_view key) {
    return impl_->remove(key);
}
bool Client::contains(std::string_view key) {
    return impl_->contains(key);
}
std::size_t Client::size() {
    return impl_->size();
}
void Client::clear() {
    impl_->clear();
}
bool Client::ping() {
    return impl_->ping();
}
}  // namespace kvstore::net