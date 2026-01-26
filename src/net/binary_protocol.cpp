#include "kvstore/net/binary_protocol.hpp"
#include "kvstore/util/binary_io.hpp"

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

namespace util = kvstore::util;

std::vector<uint8_t> BinaryProtocol::encode_request(const Request& req) {
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>(req.command));  // 1 byte command

    switch (req.command) {
        case Command::Get:
        case Command::Del:
        case Command::Exists:
            util::write_string(payload, req.key);  // just key
            break;

        case Command::Put:
            util::write_string(payload, req.key);
            util::write_string(payload, req.value);  // key + val
            break;

        case Command::PutEx:
            util::write_string(payload, req.key);
            util::write_string(payload, req.value);
            util::write_uint64_be(payload, static_cast<uint64_t>(req.ttl_ms));  // key + val + ttl
            break;
        default:
            break;
    }

    std::vector<uint8_t> result;
    result.reserve(4 + payload.size());
    util::write_uint32_be(result, static_cast<uint32_t>(payload.size()));  // prepend length
    result.insert(result.end(), payload.begin(), payload.end());     // insert payload

    return result;
}

std::optional<Request> BinaryProtocol::decode_request(const std::vector<uint8_t>& data,
                                                            size_t& bytes_consumed) {
    // need atleast 4 bytes for length
    if (data.size() < 4) {
        return std::nullopt;
    }

    // check full msg arrived
    uint32_t msg_len = util::read_uint32_be(data.data());
    if (data.size() < 4 + msg_len) {
        return std::nullopt;
    }

    // tell caller how much we used
    bytes_consumed = 4 + msg_len;

    if (msg_len < 1) {
        throw std::runtime_error("Empty message");
    }

    Request req;
    // get request command from first byte
    req.command = static_cast<Command>(data[4]);

    size_t offset = 5;  // start of command-specific data
    size_t max_offset = 4 + msg_len;

    switch (req.command) {
        case Command::Get:
        case Command::Del:
        case Command::Exists:
            req.key = util::read_string(data.data(), offset, max_offset);
            break;

        case Command::Put:
            req.key = util::read_string(data.data(), offset, max_offset);
            req.value = util::read_string(data.data(), offset, max_offset);
            break;

        case Command::PutEx:
            req.key = util::read_string(data.data(), offset, max_offset);
            req.value = util::read_string(data.data(), offset, max_offset);
            if (offset + 8 > max_offset) {
                throw std::runtime_error("Incomplete TTL");
            }
            req.ttl_ms = static_cast<int64_t>(util::read_uint64_be(data.data() + offset));
            offset += 8;
            break;

        case Command::Size:
        case Command::Clear:
        case Command::Ping:
        case Command::Quit:
            // No payload
            break;

        default:
            throw std::runtime_error("Unknown command");
    }

    return req;
}

std::vector<uint8_t> BinaryProtocol::encode_response(const Response& resp) {
    std::vector<uint8_t> payload;

    // 1 bytes status
    payload.push_back(static_cast<uint8_t>(resp.status));

    if (!resp.data.empty()) {
        util::write_string(payload, resp.data);  // optional data
    }

    std::vector<uint8_t> result;
    result.reserve(4 + payload.size());
    util::write_uint32_be(result, static_cast<uint32_t>(payload.size()));
    result.insert(result.end(), payload.begin(), payload.end());

    return result;
}

std::optional<Response> BinaryProtocol::decode_response(const std::vector<uint8_t>& data,
                                                              size_t& bytes_consumed) {
    if (data.size() < 4) {
        return std::nullopt;
    }

    // get length
    uint32_t msg_len = util::read_uint32_be(data.data());
    if (data.size() < 4 + msg_len) {
        return std::nullopt;
    }

    bytes_consumed = 4 + msg_len;

    if (msg_len < 1) {
        throw std::runtime_error("Empty response");
    }

    Response resp;
    // response status is first byte after length
    resp.status = static_cast<Status>(data[4]);
    resp.close_connection = (resp.status == Status::Bye);

    // read the rest of the payload (optional string data)
    if (msg_len > 1) {
        size_t offset = 5;
        resp.data = util::read_string(data.data(), offset, 4 + msg_len);
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
    uint32_t msg_len = util::read_uint32_be(data.data());
    return data.size() >= 4 + msg_len;
}

uint32_t BinaryProtocol::peek_message_length(const std::vector<uint8_t>& data) {
    // cant read length, return 0
    if (data.size() < 4) {
        return 0;
    }
    return util::read_uint32_be(data.data());
}

}  // namespace kvstore::net