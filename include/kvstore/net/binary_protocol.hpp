#ifndef KVSTORE_NET_BINARY_PROTOCOL_HPP
#define KVSTORE_NET_BINARY_PROTOCOL_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kvstore::net {

enum class BinaryCommand : uint8_t {
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

enum class BinaryStatus : uint8_t {
    Ok = 0,
    NotFound = 1,
    Error = 2,
    Bye = 3,
};

struct BinaryRequest {
    BinaryCommand command;
    std::string key;
    std::string value;
    int64_t ttl_ms = 0;
};

struct BinaryResponse {
    BinaryStatus status;
    std::string data;
    bool close_connection = false;
};

class BinaryProtocol {
   public:
    // encode request to bytes
    static std::vector<uint8_t> encode_request(const BinaryRequest& req);

    // decode request from bytes (returns nullopt if incomplete)
    static std::optional<BinaryRequest> decode_request(const std::vector<uint8_t>& data,
                                                       size_t& bytes_consumed);

    // encode response to bytes
    static std::vector<uint8_t> encode_response(const BinaryResponse& resp);

    // decode response form bytes (returns nullopt if incomplete)
    static std::optional<BinaryResponse> decode_response(const std::vector<uint8_t>& data,
                                                         size_t& bytes_consumed);

    // heler: check if buffer has complete message
    static bool has_complete_message(const std::vector<uint8_t>& data);

    // helper: get message length from header
    static uint32_t peek_message_length(const std::vector<uint8_t>& data);
};

}  // namespace kvstore::net

#endif