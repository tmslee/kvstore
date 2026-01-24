#include "kvstore/net/binary_protocol.hpp"

#include <cstring>
#include <stdexcept>

/*
    write format:
    - message: [4 bytes length][payload]
    - request paylod: [1 byte: command][... command specific data]
    - response payload: [1 byte status][optional: string data]
    all multi byte integers are big-endian (network byte order)
    note: 1 hex digit = 4 bits
*/

namespace kvstore::net {

namespace {
void write_uint32_be(std::vector<uint8_t>& buf, uint32_t value) {
    buf.push_back((value >> 24) & 0xFF);  // most significant byte
    buf.push_back((value >> 16) & 0xFF);
    buf.push_back((value >> 8) & 0xFF);
    buf.push_back(value & 0xFF);  // least significant byte
    /*
        example: value = 0x12345678
        buf: [0x12, 0x34, 0x56, 0x78]
    */
}

uint32_t read_uint32_be(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
    /*
        reassemble bytes into integer example:
        data: [0x12, 0x34, 0x56, 0x78]
        return: 0x12345678
    */
}

void write_uint64_be(std::vector<uint8_t>& buf, uint64_t value) {
    // similar to write_uint32)be but with 64 bits.
    for (int i = 7; i >= 0; --i) {
        buf.push_back((value >> (i * 8)) & 0xFF);
    }
}

uint64_t read_uint64_be(const uint8_t* data) {
    // same concept as read_uint32_be but with 64 bits
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

void write_string(std::vector<uint8_t>& buf, std::string_view s) {
    write_uint32_be(buf, static_cast<uint32_t>(s.size()));  // write length prefix
    buf.insert(buf.end(), s.begin(), s.end());              // write raw bytes
}

std::string read_string(const uint8_t* data, size_t& offset, size_t max_size) {
    // 1. read 4 byte length
    if (offset + 4 > max_size) {
        throw std::runtime_error("Incomplete string length");
    }
    uint32_t len = read_uint32_be(data + offset);
    offset += 4;

    if (offset + len > max_size) {
        throw std::runtime_error("Incomplete string data");
    }

    // 2. read that many bytes as string
    std::string result(reinterpret_cast<const char*>(data + offset), len);

    // 3. advance offset
    offset += len;
    return result;
}

}  // namespace

std::vector<uint8_t> BinaryProtocol::encode_request(const BinaryRequest& req) {
    std::vector<uint8_t> payload;

    payload.push_back(static_cast<uint8_t>(req.command));  // 1 byte command

    switch (req.command) {
        case BinaryCommand::Get:
        case BinaryCommand::Del:
        case BinaryCommand::Exists:
            write_string(payload, req.key);  // just key
            break;

        case BinaryCommand::Put:
            write_string(payload, req.key);
            write_string(payload, req.value);  // key + val
            break;

        case BinaryCommand::PutEx:
            write_string(payload, req.key);
            write_string(payload, req.value);
            write_uint64_be(payload, static_cast<uint64_t>(req.ttl_ms));  // key + val + ttl
            break;

        case BinaryCommand::Size:
        case BinaryCommand::Clear:
        case BinaryCommand::Ping:
        case BinaryCommand::Quit:
            // no payload
            break;
    }

    std::vector<uint8_t> result;
    result.reserve(4 + payload.size());
    write_uint32_be(result, static_cast<uint32_t>(payload.size()));  // prepend length
    result.insert(result.end(), payload.begin(), payload.end());     // insert payload

    return result;
}

std::optional<BinaryRequest> BinaryProtocol::decode_request(const std::vector<uint8_t>& data,
                                                            size_t& bytes_consumed) {
    // need atleast 4 bytes for length
    if (data.size() < 4) {
        return std::nullopt;
    }

    // check full msg arrived
    uint32_t msg_len = read_uint32_be(data.data());
    if (data.size() < 4 + msg_len) {
        return std::nullopt;
    }

    // tell caller how much we used
    bytes_consumed = 4 + msg_len;

    if (msg_len < 1) {
        throw std::runtime_error("Empty message");
    }

    BinaryRequest req;
    // get request command from first byte
    req.command = static_cast<BinaryCommand>(data[4]);

    size_t offset = 5;  // start of command-specific data
    size_t max_offset = 4 + msg_len;

    switch (req.command) {
        case BinaryCommand::Get:
        case BinaryCommand::Del:
        case BinaryCommand::Exists:
            req.key = read_string(data.data(), offset, max_offset);
            break;

        case BinaryCommand::Put:
            req.key = read_string(data.data(), offset, max_offset);
            req.value = read_string(data.data(), offset, max_offset);
            break;

        case BinaryCommand::PutEx:
            req.key = read_string(data.data(), offset, max_offset);
            req.value = read_string(data.data(), offset, max_offset);
            if (offset + 8 > max_offset) {
                throw std::runtime_error("Incomplete TTL");
            }
            req.ttl_ms = static_cast<int64_t>(read_uint64_be(data.data() + offset));
            offset += 8;
            break;

        case BinaryCommand::Size:
        case BinaryCommand::Clear:
        case BinaryCommand::Ping:
        case BinaryCommand::Quit:
            // No payload
            break;

        default:
            throw std::runtime_error("Unknown command");
    }

    return req;
}

std::vector<uint8_t> BinaryProtocol::encode_response(const BinaryResponse& resp) {
    std::vector<uint8_t> payload;

    // 1 bytes status
    payload.push_back(static_cast<uint8_t>(resp.status));

    if (!resp.data.empty()) {
        write_string(payload, resp.data);  // optional data
    }

    std::vector<uint8_t> result;
    result.reserve(4 + payload.size());
    write_uint32_be(result, static_cast<uint32_t>(payload.size()));
    result.insert(result.end(), payload.begin(), payload.end());

    return result;
}

std::optional<BinaryResponse> BinaryProtocol::decode_response(const std::vector<uint8_t>& data,
                                                              size_t& bytes_consumed) {
    if (data.size() < 4) {
        return std::nullopt;
    }

    // get length
    uint32_t msg_len = read_uint32_be(data.data());
    if (data.size() < 4 + msg_len) {
        return std::nullopt;
    }

    bytes_consumed = 4 + msg_len;

    if (msg_len < 1) {
        throw std::runtime_error("Empty response");
    }

    BinaryResponse resp;
    // response status is first byte after length
    resp.status = static_cast<BinaryStatus>(data[4]);
    resp.close_connection = (resp.status == BinaryStatus::Bye);

    // read the rest of the payload (optional string data)
    if (msg_len > 1) {
        size_t offset = 5;
        resp.data = read_string(data.data(), offset, 4 + msg_len);
    }

    return resp;
}

// note data() returns pointer to underlying array
bool BinaryProtocol::has_complete_message(const std::vector<uint8_t>& data) {
    // no length yet
    if (data.size() < 4) {
        return false;
    }
    // read length and see if we have full message
    uint32_t msg_len = read_uint32_be(data.data());
    return data.size() >= 4 + msg_len;
}

uint32_t BinaryProtocol::peek_message_length(const std::vector<uint8_t>& data) {
    // cant read length, return 0
    if (data.size() < 4) {
        return 0;
    }
    return read_uint32_be(data.data());
}

}  // namespace kvstore::net