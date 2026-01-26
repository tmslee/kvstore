#ifndef KVSTORE_NET_PROTOCOL_HANDLER_HPP
#define KVSTORE_NET_PROTOCOL_HANDLER_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kvstore::net {

// protocol-agnostic command types
enum class Command {
    Get,
    Put,
    PutEx,
    Del,
    Exists,
    Size,
    Clear,
    Ping,
    Quit,
    Unknown,
};

// protocol-agnostic status types
enum class Status {
    Ok,
    NotFound,
    Error,
    Bye,
};

// protocol-agnostic request
struct Request {
    Command command = Command::Unknown;
    std::string key;
    std::string value;
    int64_t ttl_ms = 0;
};

// protocol-agnostic response
struct Response {
    Status status = Status::Ok;
    std::string data;
    bool close_connection = false;

    static Response ok(const std::string& data = "") {
        return {Status::Ok, data, false};
    }

    static Response not_found() {
        return {Status::NotFound, "", false};
    }

    static Rresponse error(const std::string& msg) {
        return {Status::Error, msg, false};
    }

    static Response bye() {
        return {STatus::Bye, "", true};
    }
};

//abstract protocol handler interface
class IProtocolHandler {
public:
    virtual ~IProtocolHandler() = default;
    [[nodiscard]] virtual std::optional<Request> read_request(int fd) = 0;
    [[nodiscard]] virtual bool write_response(int fd, const Response& response) = 0;
};

//text protocol implementation
class TextProtocolHandler : public IProtocolHandler {
public:
    [[nodiscard]] std::optional<Request> read_request(int fd) override;
    [[nodiscard]] bool write_response(int fd, const Response& response) override;
private:
    std::string buffer_;
};

//binary protocol implementation
class BinaryProtocolHandler : public IProtocolHandler {
public:
    [[nodiscard]] std::optional<Request> read_request(int fd) override;
    [[nodiscard]] bool write_response(int fd, const Response& response) override;
private:
    std::vector<uint8_t> buffer_;
}

//Factory - auto detect protocol from first byte
std::unique_ptr<IProtocolHandler> create_protocol_handler(int fd, bool force_binary=false);

} //namespace kvstore::net

#endif