#ifndef KVSTORE_NET_SERVER_PROTOCOL_HANDLER_HPP
#define KVSTORE_NET_SERVER_PROTOCOL_HANDLER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "kvstore/net/types.hpp"

namespace kvstore::net::server {

struct ProtocolLimits {
    std::size_t max_message_size = 64 * 1024 * 1024;  // 64MB
    std::size_t max_key_size = 1024;                  // 1KB
    std::size_t max_value_size = 64 * 1024 * 1024;    // 64MB
    std::size_t max_line_size = 64 * 1024 * 1024;     // 64MB (text protocol)
};

class IProtocolHandler {
   public:
    virtual ~IProtocolHandler() = default;
    [[nodiscard]] virtual std::optional<Request> read_request(int fd) = 0;
    [[nodiscard]] virtual bool write_response(int fd, const Response& response) = 0;
};

class TextProtocolHandler : public IProtocolHandler {
   public:
    explicit TextProtocolHandler(const ProtocolLimits& limits = {}) : limits_(limits) {}

    [[nodiscard]] std::optional<Request> read_request(int fd) override;
    [[nodiscard]] bool write_response(int fd, const Response& response) override;

   private:
    ProtocolLimits limits_;
    std::string buffer_;
};

class BinaryProtocolHandler : public IProtocolHandler {
   public:
    explicit BinaryProtocolHandler(const ProtocolLimits& limits = {}) : limits_(limits) {}

    [[nodiscard]] std::optional<Request> read_request(int fd) override;
    [[nodiscard]] bool write_response(int fd, const Response& response) override;

   private:
    ProtocolLimits limits_;
    std::vector<uint8_t> buffer_;
};

std::unique_ptr<IProtocolHandler> create_protocol_handler(int fd, bool force_binary = false,
                                                          const ProtocolLimits& limits = {});

}  // namespace kvstore::net::server

#endif