#ifndef KVSTORE_NET_SERVER_PROTOCOL_HANDLER_HPP
#define KVSTORE_NET_SERVER_PROTOCOL_HANDLER_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "kvstore/net/types.hpp"

namespace kvstore::net::server {

class IProtocolHandler {
   public:
    virtual ~IProtocolHandler() = default;
    [[nodiscard]] virtual std::optional<Request> read_request(int fd) = 0;
    [[nodiscard]] virtual bool write_response(int fd, const Response& response) = 0;
};

class TextProtocolHandler : public IProtocolHandler {
   public:
    [[nodiscard]] std::optional<Request> read_request(int fd) override;
    [[nodiscard]] bool write_response(int fd, const Response& response) override;

   private:
    std::string buffer_;
};

class BinaryProtocolHandler : public IProtocolHandler {
   public:
    [[nodiscard]] std::optional<Request> read_request(int fd) override;
    [[nodiscard]] bool write_response(int fd, const Response& response) override;

   private:
    std::vector<uint8_t> buffer_;
};

std::unique_ptr<IProtocolHandler> create_protocol_handler(int fd, bool force_binary = false);

}  // namespace kvstore::net::server

#endif