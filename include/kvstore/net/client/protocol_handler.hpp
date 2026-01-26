#ifndef KVSTORE_NET_CLIENT_PROTOCOL_HANDLER_HPP
#define KVSTORE_NET_CLIENT_PROTOCOL_HANDLER_HPP

#include "kvstore/net/types.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kvstore::net::client {

class IProtocolHandler {
public:
    virtual ~IProtocolHandler() = default;

    [[nodiscard]] virtual bool write_request(int fd, const Request& request) = 0;
    [[nodiscard]] virtual std::optional<Response> read_response(int fd) = 0;
};

class TextProtocolHandler : public IProtocolHandler {
public:
    [[nodiscard]] bool write_request(int fd, const Request& request) override;
    [[nodiscard]] std::optional<Response> read_response(int fd) override;

private:
    std::string buffer_;
};

class BinaryProtocolHandler : public IProtocolHandler {
public:
    [[nodiscard]] bool write_request(int fd, const Request& request) override;
    [[nodiscard]] std::optional<Response> read_response(int fd) override;

private:
    std::vector<uint8_t> buffer_;
};

std::unique_ptr<IProtocolHandler> create_protocol_handler(bool binary);

}  // namespace kvstore::net::client

#endif