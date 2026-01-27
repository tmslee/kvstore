#include "kvstore/net/client/client.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stdexcept>

#include "kvstore/net/client/protocol_handler.hpp"
#include "kvstore/net/types.hpp"

namespace kvstore::net::client {

namespace util = kvstore::util;

class Client::Impl {
   public:
    explicit Impl(const ClientOptions& options)
        : options_(options), protocol_(create_protocol_handler(options.binary)) {}

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
        note: we throw in user operations because errors are rare and unrecoverable (network down =
    cant continue anyway)
    */
    void put(std::string_view key, std::string_view value) {
        auto resp = execute({Command::Put, std::string(key), std::string(value), 0});
        if (resp.status != Status::Ok) {
            throw std::runtime_error("PUT failed: " + resp.data);
        }
    }

    void put(std::string_view key, std::string_view value, util::Duration ttl) {
        auto resp = execute({Command::PutEx, std::string(key), std::string(value), ttl.count()});
        if (resp.status != Status::Ok) {
            throw std::runtime_error("PUTEX failed: " + resp.data);
        }
    }

    [[nodiscard]] std::optional<std::string> get(std::string_view key) {
        auto resp = execute({Command::Get, std::string(key), "", 0});
        if (resp.status == Status::NotFound) {
            return std::nullopt;
        }
        if (resp.status != Status::Ok) {
            throw std::runtime_error("GET failed: " + resp.data);
        }
        return resp.data;
    }

    [[nodiscard]] bool remove(std::string_view key) {
        auto resp = execute({Command::Del, std::string(key), "", 0});
        if (resp.status == Status::NotFound) {
            return false;
        }
        if (resp.status != Status::Ok) {
            throw std::runtime_error("DEL failed: " + resp.data);
        }
        return true;
    }

    [[nodiscard]] bool contains(std::string_view key) {
        auto resp = execute({Command::Exists, std::string(key), "", 0});
        if (resp.status != Status::Ok) {
            throw std::runtime_error("EXISTS failed: " + resp.data);
        }
        return resp.data == "1";
    }

    [[nodiscard]] std::size_t size() {
        auto resp = execute({Command::Size, "", "", 0});
        if (resp.status != Status::Ok) {
            throw std::runtime_error("SIZE failed: " + resp.data);
        }
        return std::stoull(resp.data);
    }

    void clear() {
        auto resp = execute({Command::Clear, "", "", 0});
        if (resp.status != Status::Ok) {
            throw std::runtime_error("CLEAR failed: " + resp.data);
        }
    }

    [[nodiscard]] bool ping() {
        try {
            auto resp = execute({Command::Ping, "", "", 0});
            return resp.status == Status::Ok && resp.data == "PONG";
        } catch (...) {
            return false;
        }
    }

   private:
    Response execute(const Request& req) {
        if (socket_fd_ < 0) {
            throw std::runtime_error("Not connected");
        }

        if (!protocol_->write_request(socket_fd_, req)) {
            disconnect();
            throw std::runtime_error("failed to send request");
        }

        auto resp = protocol_->read_response(socket_fd_);
        if (!resp) {
            disconnect();
            throw std::runtime_error("Failed to receive response");
        }

        return *resp;
    }

    ClientOptions options_;
    std::unique_ptr<IProtocolHandler> protocol_;
    int socket_fd_ = -1;
};

// PIMPL INTERFACE ------------------------------------------------------------------------
Client::Client(const ClientOptions& options) : impl_(std::make_unique<Impl>(options)) {}
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
}  // namespace kvstore::net::client