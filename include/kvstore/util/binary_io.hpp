#ifndef KVSTORE_UTIL_BINARY_IO_HPP
#define KVSTORE_UTIL_BINARY_IO_HPP

#include <unistd.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kvstore::util {

// Maximum string size for file I/O to prevent memory exhaustion from malformed data
// 256MB is generous for key-value data while preventing DoS from corrupted length prefixes
constexpr uint32_t kMaxStringSize = 256 * 1024 * 1024;

/*
    Binary I/O utilities for the kvstore project.

    Two I/O patterns are supported:
    1. POSIX fd-based I/O - for file persistence (WAL, snapshot, disk_store)
       Uses raw file descriptors with fsync() for durability guarantees.

    2. Buffer-based I/O - for network protocol (binary_protocol)
       Uses std::vector<uint8_t> for building/parsing network messages.

    Note: we use uint32 for string lengths and uint64 for entry counts.
    When we read/write strings we always do length-prefix then data.
*/

// ============================================================================
// POSIX fd-based I/O (for file persistence with fsync support)
// ============================================================================

template <typename T>
void write_int_fd(int fd, T value) {
    static_assert(std::is_integral_v<T>, "T must be integral");
    ssize_t written = write(fd, &value, sizeof(value));
    if (written != static_cast<ssize_t>(sizeof(value))) {
        throw std::runtime_error("fd write failed");
    }
}

inline void write_string_fd(int fd, std::string_view str) {
    write_int_fd<uint32_t>(fd, static_cast<uint32_t>(str.size()));
    if (!str.empty()) {
        ssize_t written = write(fd, str.data(), str.size());
        if (written != static_cast<ssize_t>(str.size())) {
            throw std::runtime_error("fd write failed");
        }
    }
}

template <typename T>
bool read_int_fd(int fd, T& value) {
    static_assert(std::is_integral_v<T>, "T must be integral");
    ssize_t bytes_read = read(fd, &value, sizeof(value));
    return bytes_read == static_cast<ssize_t>(sizeof(value));
}

inline bool read_string_fd(int fd, std::string& str, uint32_t max_size = kMaxStringSize) {
    uint32_t len;
    if (!read_int_fd<uint32_t>(fd, len)) {
        return false;
    }
    if (len > max_size) {
        return false;  // reject oversized strings to prevent memory exhaustion
    }
    str.resize(len);
    if (len == 0) {
        return true;
    }
    ssize_t bytes_read = read(fd, str.data(), len);
    return bytes_read == static_cast<ssize_t>(len);
}

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

inline std::string read_string(const uint8_t* data, size_t& offset, size_t max_size=kMaxStringSize) {
    uint32_t len = read_int<uint32_t>(data, offset, max_size);
    if (offset + len > max_size) {
        throw std::runtime_error("Buffer underflow reading string");
    }
    std::string result(reinterpret_cast<const char*>(data + offset), len);
    offset += len;
    return result;
}

}  // namespace kvstore::util

#endif
