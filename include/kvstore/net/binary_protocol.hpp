#ifndef KVSTORE_NET_BINARY_PROTOCOL_HPP
#define KVSTORE_NET_BINARY_PROTOCOL_HPP

#include "kvstore/net/types.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace kvstore::net {

class BinaryProtocol {
   public:
    // encode
    static std::vector<uint8_t> encode_request(const Request& req);
    static std::vector<uint8_t> encode_response(const Response& resp);

    //decode (return nullopt if incomplete)
    static std::optional<Request> decode_request(const std::vector<uint8_t>& data,
                                                       size_t& bytes_consumed);
    static std::optional<Response> decode_response(const std::vector<uint8_t>& data,
                                                         size_t& bytes_consumed);

    // helpers
    static bool has_complete_message(const std::vector<uint8_t>& data);
    static uint32_t peek_message_length(const std::vector<uint8_t>& data);
};

}  // namespace kvstore::net

#endif