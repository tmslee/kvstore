#ifndef KVSTORE_UTIL_BINARY_IO_HPP
#define KVSTORE_UTIL_BINARY_IO_HPP

#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

template <typename T>
void write_int(std::ostream& out, T value) {
    static_assert(std::is_integral_v<T>, "T must be integral");
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    if (!out.good()) {
        throw std::runtime_error("Stream write failed");
    }
}

template <typename T>
bool read_int(std::istream& in, T& value) {
    static_assert(std::is_integral_v<T>, "T must be integral");
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return in.good();
}

inline void write_string(std::ostream& out, std::string_view str) {
    write_int<uint32_t>(out, static_cast<uint32_t>(str.size()));
    out.write(str.data(), static_cast<std::streamsize>(str.size()));
    if (!out.good()) {
        throw std::runtime_error("Stream write failed");
    }
}

inline bool read_string(std::istream& in, std::string& str) {
    uint32_t len;
    read_int<uint32_t>(in, len);
    str.resize(len);
    in.read(str.data(), len);
    return in.good();
}

// inline void write_uint8(std::ostream& out, uint8_t value) {
//     out.write(reinterpret_cast<const char*>(&value), sizeof(value));
// }

// inline void write_uint32(std::ostream& out, uint32_t value) {
//     out.write(reinterpret_cast<const char*>(&value), sizeof(value));
// }

// inline void write_uint64(std::ostream& out, uint64_t value) {
//     out.write(reinterpret_cast<const char*>(&value), sizeof(value));
// }

// inline void write_int64(std::ostream& out, int64_t value) {
//     out.write(reinterpret_cast<const char*>(&value), sizeof(value));
// }

// inline uint8_t read_uint8(std::istream& in) {
//     uint8_t value = 0;
//     in.read(reinterpret_cast<char*>(&value), sizeof(value));
//     return value;
// }

// inline uint32_t read_uint32(std::istream& in) {
//     uint32_t value = 0;
//     in.read(reinterpret_cast<char*>(&value), sizeof(value));
//     return value;
// }

// inline uint64_t read_uint64(std::istream& in) {
//     uint64_t value = 0;
//     in.read(reinterpret_cast<char*>(&value), sizeof(value));
//     return value;
// }

// inline int64_t read_int64(std::istream& in) {
//     int64_t value = 0;
//     in.read(reinterpret_cast<char*>(&value), sizeof(value));
//     return value;
// }

// ============================================================================
// Buffer-based I/O (for network - binary protocol)
// ============================================================================

template <typename T>
void write_int(std::vector<uint8_t>& buf, T value) {
    static_assert(std::is_integral_v<T>, "T must be integral");
    for (int i = sizeof(T) - 1; i >= 0; --i) {
        buf.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

template <typename T>
T read_int(const uint8_t* data, size_t& offset, size_t max_size) {
    static_assert(std::is_integral_v<T>, "T must be integral");
    if (offset + sizeof(T) > max_size) {
        throw std::runtime_error("Buffer underflow reading integer");
    }
    T value = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        value = (value << 8) | data[offset++];
    }
    return value;
}

template <typename T>
T read_int(const uint8_t* data) {
    static_assert(std::is_integral_v<T>, "T must be integral");
    T value = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

inline void write_string(std::vector<uint8_t>& buf, std::string_view s) {
    write_int<uint32_t>(buf, static_cast<uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

inline std::string read_string(const uint8_t* data, size_t& offset, size_t max_size) {
    uint32_t len = read_int<uint32_t>(data, offset, max_size);
    if (offset + len > max_size) {
        throw std::runtime_error("Buffer underflow reading string");
    }
    std::string result(reinterpret_cast<const char*>(data + offset), len);
    offset += len;
    return result;
}

// inline void write_uint32_be(std::vector<uint8_t>& buf, uint32_t value) {
//     buf.push_back((value >> 24) & 0xFF);  // most significant byte
//     buf.push_back((value >> 16) & 0xFF);
//     buf.push_back((value >> 8) & 0xFF);
//     buf.push_back(value & 0xFF);  // least significant byte
//     /*
//         example: value = 0x12345678
//         buf: [0x12, 0x34, 0x56, 0x78]
//     */
// }

// inline void write_uint64_be(std::vector<uint8_t>& buf, uint64_t value) {
//     // similar to write_uint32)be but with 64 bits.
//     for (int i = 7; i >= 0; --i) {
//         buf.push_back((value >> (i * 8)) & 0xFF);
//     }
// }

// inline void write_string(std::vector<uint8_t>& buf, std::string_view s) {
//     write_uint32_be(buf, static_cast<uint32_t>(s.size()));  // write length prefix
//     buf.insert(buf.end(), s.begin(), s.end());              // write raw bytes
// }

// inline uint32_t read_uint32_be(const uint8_t* data) {
//     return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
//            (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
//     /*
//         reassemble bytes into integer example:
//         data: [0x12, 0x34, 0x56, 0x78]
//         return: 0x12345678
//     */
// }

// inline uint64_t read_uint64_be(const uint8_t* data) {
//     // same concept as read_uint32_be but with 64 bits
//     uint64_t value = 0;
//     for (int i = 0; i < 8; ++i) {
//         value = (value << 8) | data[i];
//     }
//     return value;
// }

// inline std::string read_string(const uint8_t* data, size_t& offset, size_t max_size) {
//     // 1. read 4 byte length
//     if (offset + 4 > max_size) {
//         throw std::runtime_error("Incomplete string length");
//     }
//     uint32_t len = read_uint32_be(data + offset);
//     offset += 4;

//     if (offset + len > max_size) {
//         throw std::runtime_error("Incomplete string data");
//     }

//     // 2. read that many bytes as string
//     std::string result(reinterpret_cast<const char*>(data + offset), len);

//     // 3. advance offset
//     offset += len;
//     return result;
// }

}  // namespace kvstore::util

#endif