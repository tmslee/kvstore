#include "kvstore/core/snapshot.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <stdexcept>
#include <cstring>

#include "kvstore/util/logger.hpp"
#include "kvstore/util/binary_io.hpp"
#include "kvstore/util/fd_guard.hpp"

namespace kvstore::core {

namespace util = kvstore::util;

Snapshot::Snapshot(const std::filesystem::path& path) : path_(path) {}

void Snapshot::save(const EntryIterator& iterate) {
    // write to temp file first to make snapshotting atomic. crash mid write will keep original
    // snapshot safe
    std::filesystem::path temp_path = path_.string() + ".tmp";

    util::FdGuard fd_guard(open(temp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644));
    if (!fd_guard) {
        throw std::runtime_error("failed to open snapshot file: " + temp_path.string());
    }
    int fd = fd_guard.get();

    // write header. magic number for file type, version for format compatibility
    util::write_int_fd<uint32_t>(fd, kMagic);
    util::write_int_fd<uint32_t>(fd, kVersion);

    // remember position for count (we'll update it later)
    off_t count_pos = lseek(fd, 0, SEEK_CUR);

    // write placeholder for count (we dont know yet)
    util::write_int_fd<uint64_t>(fd, 0);

    std::size_t count = 0;
    // call iterator & pass the lambda which is the entry emitter
    iterate([fd, &count](std::string_view key, std::string_view value,
                         util::ExpirationTime expires_at) {
        util::write_string_fd(fd, key);
        util::write_string_fd(fd, value);
        uint8_t has_expiration = expires_at.has_value() ? 1 : 0;
        util::write_int_fd<uint8_t>(fd, has_expiration);
        if (expires_at.has_value()) {
            util::write_int_fd<uint64_t>(fd, expires_at.value());
        }
        ++count;
    });

    // seek back and update our entry count
    lseek(fd, count_pos, SEEK_SET);
    util::write_int_fd<uint64_t>(fd, count);

    // fsync to ensure data is on physical disk before rename
    if (fsync(fd) != 0) {
        throw std::runtime_error("failed to fsync snapshot file");
    }

    // fd_guard will close fd when it goes out of scope
    entry_count_ = count;

    // rename temp to final (atomic on POSIX)
    std::filesystem::rename(temp_path, path_);

    // fsync the directory to ensure rename is durable
    std::filesystem::path dir_path = path_.parent_path();
    if (dir_path.empty()) {
        dir_path = ".";
    }
    util::FdGuard dir_guard(open(dir_path.c_str(), O_RDONLY));
    if (dir_guard) {
        if (fsync(dir_guard.get()) != 0) {
            LOG_WARN("failed to fsync directory after snapshot rename: " + std::string(strerror(errno)));
        }
    }
}

void Snapshot::load(
    std::function<void(std::string_view, std::string_view, util::ExpirationTime)> callback) {
    util::FdGuard fd_guard(open(path_.c_str(), O_RDONLY));
    if (!fd_guard) {
        return;
    }
    int fd = fd_guard.get();

    // verify header
    if (!validate_header(fd)) {
        throw std::runtime_error("Invalid snapshot file: bad header");
    }

    uint64_t count;
    if (!util::read_int_fd<uint64_t>(fd, count)) {
        throw std::runtime_error("corrupt snapshot file");
    }

    std::string key;
    std::string value;

    // get each entry and pass to callback. store will insert to map
    for (uint64_t i = 0; i < count; ++i) {
        if (!util::read_string_fd(fd, key) || !util::read_string_fd(fd, value)) {
            throw std::runtime_error("corrupted snapshot file");
        }
        uint8_t has_expiration;
        if (!util::read_int_fd<uint8_t>(fd, has_expiration)) {
            throw std::runtime_error("corrupted snapshot file");
        }

        util::ExpirationTime expires_at = std::nullopt;
        if (has_expiration != 0) {
            int64_t expires_at_ms;
            if (!util::read_int_fd<int64_t>(fd, expires_at_ms)) {
                throw std::runtime_error("corrupted snapshot file");
            }
            expires_at = expires_at_ms;
        }
        callback(key, value, expires_at);
    }

    entry_count_ = count;
}

bool Snapshot::exists() const {
    return std::filesystem::exists(path_);
}

std::filesystem::path Snapshot::path() const {
    return path_;
}

std::size_t Snapshot::entry_count() const {
    return entry_count_;
}

bool Snapshot::validate_header(int fd) {
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

}  // namespace kvstore::core
