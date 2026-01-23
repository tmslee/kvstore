#include "kvstore/net/binary_protocol.hpp"
#include <cstring>
#include <stdexcept>

namespace kvstore::net {

namespace {
    void write_uint32_be(std::vector<uint8_t>&buf, uint32_t value){
        buf.push_back((value >> 24) & 0xFF);
        buf.push_back((value >> 16) & 0xFF);
        buf.push_back((value >> 8) & 0xFF);
        buf.push_back(value & 0xFF);
    }

    uint32_t read_uint32_be(const uint8_t* data) {
        return (static_cast<uint32_t>(data[0]) << 24) |
            (static_cast<uint32_t>(data[1]) << 16) |
            (static_cast<uint32_t>(data[2]) << 8) |
            static_cast<uint32_t>(data[3]);
    }

    void write_uint64_be(std::vector<uint8_t>& buf, uint64_t value) {
        for(int i=7; i>=0; --i) {
            buf.push_back((value >> (i*8)) & 0xFF);
        }
    }

    uint64_t read_uint64_be(const uint8_t* data) {
        uint64_t value = 0;
        for(int i=0; i<8; ++i) {
            value = (value << 8) | data[i];
        }
        return value;
    }

    void write_string(std::vector<uint8_t>& buf, std::string_view s) {
        write_uint32_be(buf, static_cast<uint32_t>(s.size()));
        buf.insert(buf.end(), s.begin(), s.end());
    }

    std::string read_string(const uint8_t* data, size_t& offset, size_t max_size) {
        if(offset + 4 > max_size) {
            throw std::runtime_error("Incomplete string length");
        }
        uint32_t len = read_uint32_be(data + offset);
        offset += 4;

        if(offset + len > max_size) {
            throw std::runtime_error("Incomplete string data");
        }
        std::string result(reinterpret_cast<const char*>(data + offset), len);
        offset += len;
        return result;
    }

} //namespace

std::vector<uint8_t> BinaryProtocol::encode_request(const BinaryRequest& req) {}

std::optional<BinaryRequest> BinaryProtocol::decode_request(const std::vector<uint8_t>& data, size_t& bytes_consumed){}

std::vector<uint8_t> BinaryProtocol::encode_response(const BinaryResponse& resp) {}

std::optional<BinaryResponse> BinaryProtocol::decode_response(const std::vector<uint8_t>&data, size_t& bytes_consumed) {}

bool BinaryProtocol::has_complete_message(const std::vector<uint8_t>& data) {}

uint32_t BinaryProtocol::peek_message_length(const std::vector<uint8_t>& data) {}

} //namespace kvstore::net