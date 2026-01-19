#ifndef KVSTORE_CLIENT_HPP
#define KVSTORE_CLIENT_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kvstore {

struct ClientOptions {
    std::string host = "127.0.0.1";
    uint16_t port = 6379;
    int timeout_seconds = 30;
};

class Client {
public:
    explicit Client(const ClientOptions& options = {});
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;

    void connect();
    void disconnect();
    [[nodiscard]] bool connected() const noexcept;

    void put(std::string_view key, std::string_view value);
    [[nodiscard]] std::optional<std::string> get(std::string_view key);
    [[nodiscard]] bool remove(std::string_view key);
    [[nodiscard]] std::size_t size();
    void clear();
    [[nodiscard]] bool ping();

private:
    std::string send_command(const std::string& command);
    std::string read_response();

    ClientOptions options_;
    int socket_fd_ = -1;
};

} //namespace kvstore

#endif