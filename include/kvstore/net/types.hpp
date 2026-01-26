#ifndef KVSTORE_NET_TYPES_HPP
#define KVSTORE_NET_TYPES_HPP

#include <cstdint>
#include <string>

namespace kvstore::net {

// protocol-agnostic command types
enum class Command : uint8_t {
    Unknown = 0,
    Get = 1,
    Put = 2,
    PutEx = 3,
    Del = 4,
    Exists = 5,
    Size = 6,
    Clear = 7,
    Ping = 8,
    Quit = 9,
};

// protocol-agnostic status types
enum class Status : uint8_t {
    Ok = 0,
    NotFound = 1,
    Error = 2,
    Bye = 3,
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

    static Response error(const std::string& msg) {
        return {Status::Error, msg, false};
    }

    static Response bye() {
        return {Status::Bye, "", true};
    }
};

}
#endif