#include "kvstore/net/binary_protocol.hpp"
#include <cstring>
#include <stdexcept>

namespace kvstore::net {

namespace {
    void write_uint32_be(std::vector<uint8_t>&buf, uint32_t value){}

    uint32_t read_uint32_be(const uint8_t* data) {}

    void write_uint64_be(std::vector<uint8_t>& buf, uint64_t value) {}

    uint64_t read_uint64_be(const uint8_t* data) {}

    void write_string(std::vector<uint8_t>& buf, std::string_view s) {}

    std::string read_string(const uint8_t* data, size_t& offset, size_t max_size) {}

} //namespace

std::vector<uint8_t> BinaryProtocol::encode_request(const BinaryRequest& req) {}

std::optional<BinaryRequest> BinaryProtocol::decode_request(const std::vector<uint8_t>& data, size_t& bytes_consumed){}

std::vector<uint8_t> BinaryProtocol::encode_response(const BinaryResponse& resp) {}

std::optional<BinaryResponse> BinaryProtocol::decode_response(const std::vector<uint8_t>&data, size_t& bytes_consumed) {}

bool BinaryProtocol::has_complete_message(const std::vector<uint8_t>& data) {}

uint32_t BinaryProtocol::peek_message_length(const std::vector<uint8_t>& data) {}

} //namespace kvstore::net