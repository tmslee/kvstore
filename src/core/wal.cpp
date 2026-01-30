#include "kvstore/core/wal.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <stdexcept>

#include "kvstore/util/binary_io.hpp"

namespace kvstore::core {

namespace util = kvstore::util;

WriteAheadLog::WriteAheadLog(const std::filesystem::path& path) : path_(path) {
    bool file_exists = std::filesystem::exists(path_);

    // O_WRONLY: write only
    // O_CREAT: create if doesn't exist
    // O_APPEND: all writes go to end of file
    // 0644: owner rw, group r, others r
    fd_ = open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (fd_ < 0) {
        throw std::runtime_error("failed to open WAL file: " + path.string());
    }

    if (!file_exists || std::filesystem::file_size(path_) == 0) {
        write_header();
    }
}

WriteAheadLog::~WriteAheadLog() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

/*
    note: std::mutex cannot be moved/copied.
    keeping move ctor default makes compiler try to move each member, but mutex cannot be moved
    manually implement move ctor and assignment.
    mutex is just default constructed fresh in new object
*/
WriteAheadLog::WriteAheadLog(WriteAheadLog&& other) noexcept
    : path_(std::move(other.path_)), fd_(other.fd_) {
    other.fd_ = -1;  // source gives up ownership
}

WriteAheadLog& WriteAheadLog::operator=(WriteAheadLog&& other) noexcept {
    if (this != &other) {
        // close existing fd before taking ownership of new one
        if (fd_ >= 0) {
            close(fd_);
        }
        path_ = std::move(other.path_);
        fd_ = other.fd_;
        other.fd_ = -1;  // source gives up ownership
    }
    return *this;
}

void WriteAheadLog::write_header() {
    util::write_int_fd<uint32_t>(fd_, kMagic);
    util::write_int_fd<uint32_t>(fd_, kVersion);
    fsync(fd_);
}

bool WriteAheadLog::validate_header(int fd) {
    uint32_t magic;
    if (!util::read_int_fd<uint32_t>(fd, magic) || magic != kMagic) {
        return false;
    }

    uint32_t version;
    if (!util::read_int_fd<uint32_t>(fd, version) || version != kVersion) {
        return false;
    }
    return true;
}

void WriteAheadLog::log_put(std::string_view key, std::string_view value) {
    std::lock_guard lock(mutex_);
    write_entry(EntryType::Put, key, value);
}

void WriteAheadLog::log_put_with_ttl(std::string_view key, std::string_view value,
                                     int64_t expires_at_ms) {
    std::lock_guard lock(mutex_);
    write_entry_with_ttl(EntryType::PutWithTTL, key, value, expires_at_ms);
}

void WriteAheadLog::log_remove(std::string_view key) {
    std::lock_guard lock(mutex_);
    write_entry(EntryType::Remove, key, "");
}

void WriteAheadLog::log_clear() {
    std::lock_guard lock(mutex_);
    write_entry(EntryType::Clear, "", "");
}

void WriteAheadLog::write_entry(EntryType type, std::string_view key, std::string_view value) {
    util::write_int_fd<uint8_t>(fd_, static_cast<uint8_t>(type));
    util::write_string_fd(fd_, key);
    util::write_string_fd(fd_, value);
    // no fsync here - caller can use sync() for durability
}

void WriteAheadLog::write_entry_with_ttl(EntryType type, std::string_view key,
                                         std::string_view value, int64_t expires_at_ms) {
    util::write_int_fd<uint8_t>(fd_, static_cast<uint8_t>(type));
    util::write_string_fd(fd_, key);
    util::write_string_fd(fd_, value);
    util::write_int_fd<uint64_t>(fd_, expires_at_ms);
    // no fsync here - caller can use sync() for durability
}

bool WriteAheadLog::read_entry(int fd, EntryType& type, std::string& key, std::string& value,
                               util::ExpirationTime& expires_at) {
    // we return bool instead of throwing because end of file is expected, not exceptional
    // not being able to read successfully is an expected pattern eventually
    uint8_t type_byte;
    if (!util::read_int_fd<uint8_t>(fd, type_byte)) {
        return false;
    }

    type = static_cast<EntryType>(type_byte);
    if (!util::read_string_fd(fd, key)) {
        return false;
    }
    if (!util::read_string_fd(fd, value)) {
        return false;
    }
    if (type == EntryType::PutWithTTL) {
        int64_t expires_at_ms;
        if (!util::read_int_fd<int64_t>(fd, expires_at_ms)) {
            return false;
        }
        expires_at = expires_at_ms;
    } else {
        expires_at = std::nullopt;
    }
    return true;
}

void WriteAheadLog::replay(
    std::function<void(EntryType, std::string_view, std::string_view, util::ExpirationTime)>
        callback) {
    std::lock_guard lock(mutex_);

    int read_fd = open(path_.c_str(), O_RDONLY);
    if (read_fd < 0) {
        return;
    }

    if (!validate_header(read_fd)) {
        close(read_fd);
        throw std::runtime_error("Invalid WAL file: bad header");
    }

    EntryType type{};
    std::string key;
    std::string value;
    util::ExpirationTime expires_at;
    // try to read entry sequentially until end of file or failure
    while (read_entry(read_fd, type, key, value, expires_at)) {
        callback(type, key, value, expires_at);
    }

    close(read_fd);
}

void WriteAheadLog::sync() {
    std::lock_guard lock(mutex_);
    // fsync forces data from OS kernel buffer to physical disk
    fsync(fd_);
}

void WriteAheadLog::truncate() {
    std::lock_guard lock(mutex_);

    // close current fd
    if (fd_ >= 0) {
        close(fd_);
    }

    // reopen with O_TRUNC to delete all content
    fd_ = open(path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_ < 0) {
        throw std::runtime_error("failed to truncate WAL file: " + path_.string());
    }

    write_header();
}

std::filesystem::path WriteAheadLog::path() const {
    return path_;
}

std::size_t WriteAheadLog::size() const {
    std::lock_guard lock(mutex_);
    return std::filesystem::file_size(path_);
}

}  // namespace kvstore::core
