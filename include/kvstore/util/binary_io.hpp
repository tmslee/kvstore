#ifndef KVSTORE_UTIL_BINARY_IO_HPP
#define KVSTORE_UTIL_BINARY_IO_HPP

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>

namespace kvstore::util {
/*
    note: we have both uint64(8bytes) and uint32(4bytes) for different purposes
    - uint64 for entry count -> need more range
    - uint32 for string lengths

    when we read/write strings we always do length then data
    stream read() and write() take char*. we reinterpret_cast<const char*> to treat this integer's
   memory as raw bytes
*/

// ============================================================================
// Stream-based I/O (for files - WAL, snapshot)
// ============================================================================

inline void write_uint8(std::ostream& out, uint8_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

inline void write_uint32(std::ostream& out, uint32_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

inline void write_uint64(std::ostream& out, uint64_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

inline void write_int64(std::ostream& out, int64_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

inline void write_string(std::ostream& out, std::string_view str) {
    write_uint32(out, static_cast<uint32_t>(str.size()));
    out.write(str.data(), static_cast<std::streamsize>(str.size()));
}

inline uint8_t read_uint8(std::istream& in) {
    uint8_t value = 0;
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

inline uint32_t read_uint32(std::istream& in) {
    uint32_t value = 0;
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

inline uint64_t read_uint64(std::istream& in) {
    uint64_t value = 0;
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

inline int64_t read_int64(std::istream& in) {
    int64_t value = 0;
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

inline bool read_string(std::istream& in, std::string& str) {
    uint32_t len = read_uint32(in);
    if (!in.good()) {
        return false;
    }
    str.resize(len);
    in.read(str.data(), len);
    return in.good();
}

// ============================================================================
// Buffer-based I/O (for network - binary protocol)
// ============================================================================

inline void write_uint32_be(std::vector<uint8_t>& buf, uint32_t value) {
    buf.push_back((value >> 24) & 0xFF);  // most significant byte
    buf.push_back((value >> 16) & 0xFF);
    buf.push_back((value >> 8) & 0xFF);
    buf.push_back(value & 0xFF);  // least significant byte
    /*
        example: value = 0x12345678
        buf: [0x12, 0x34, 0x56, 0x78]
    */
}

inline void write_uint64_be(std::vector<uint8_t>& buf, uint64_t value) {
    // similar to write_uint32)be but with 64 bits.
    for (int i = 7; i >= 0; --i) {
        buf.push_back((value >> (i * 8)) & 0xFF);
    }
}

inline void write_string(std::vector<uint8_t>& buf, std::string_view s) {
    write_uint32_be(buf, static_cast<uint32_t>(s.size()));  // write length prefix
    buf.insert(buf.end(), s.begin(), s.end());              // write raw bytes
}


inline uint32_t read_uint32_be(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
    /*
        reassemble bytes into integer example:
        data: [0x12, 0x34, 0x56, 0x78]
        return: 0x12345678
    */
}

inline uint64_t read_uint64_be(const uint8_t* data) {
    // same concept as read_uint32_be but with 64 bits
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

inline std::string read_string(const uint8_t* data, size_t& offset, size_t max_size) {
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

}  // namespace kvstore::util

#endif