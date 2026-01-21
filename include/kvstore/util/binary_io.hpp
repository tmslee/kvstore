#ifndef KVSTORE_UTIL_BINARY_IO_HPP
#define KVSTORE_UTIL_BINARY_IO_HPP

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>

namespace kvstore::util {
/*
    note: we have both uint64(8bytes) and uint32(4bytes) for different purposes
    - uint64 for entry count -> need more range
    - uint32 for string lengths

    when we read/write strings we always do length then data
    stream read() and write() take char*. we reinterpret_cast<const char*> to treat this integer's
   memory as raw bytes
*/

inline void write_uint8(std::ostream& out, uint8_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

inline uint8_t read_uint8(std::istream& in) {
    uint8_t value = 0;
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

inline void write_uint32(std::ostream& out, uint32_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

inline uint32_t read_uint32(std::istream& in) {
    uint32_t value = 0;
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

inline void write_uint64(std::ostream& out, uint64_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

inline uint64_t read_uint64(std::istream& in) {
    uint64_t value = 0;
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

inline void write_int64(std::ostream& out, int64_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

inline int64_t read_int64(std::istream& in) {
    int64_t value = 0;
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

inline void write_string(std::ostream& out, std::string_view str) {
    write_uint32(out, static_cast<uint32_t>(str.size()));
    out.write(str.data(), static_cast<std::streamsize>(str.size()));
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

}  // namespace kvstore::util

#endif