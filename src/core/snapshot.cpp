#include "kvstore/core/snapshot.hpp"

#include <fstream>
#include <stdexcept>

#include "kvstore/util/binary_io.hpp"

namespace kvstore::core {

namespace util = kvstore::util;

Snapshot::Snapshot(const std::filesystem::path& path) : path_(path) {}

void Snapshot::save(const EntryIterator& iterate) {
    // write to temp file first to make snapshotting atomic. crash mid write will keep original
    // snapshot safe
    std::filesystem::path temp_path = path_.string() + ".tmp";
    {
        std::ofstream out(temp_path, std::ios::binary);
        if (!out.is_open()) {
            throw std::runtime_error("failed to open snapshot file: " + temp_path.string());
        }
        // write header. magic number for file type, version for format compatibility
        util::write_int<uint32_t>(out, kMagic);
        util::write_int<uint32_t>(out, kVersion);

        // tellp (tell put) - return current write position
        // tellg (tell get) - return current read position
        auto count_pos = out.tellp();
        // write placeholder for count (we dont know yet)
        util::write_int<uint64_t>(out, 0);

        std::size_t count = 0;
        // call iterator & pass the lambda which is the entry emitter
        iterate([&out, &count](std::string_view key, std::string_view value,
                               util::ExpirationTime expires_at) {
            util::write_string(out, key);
            util::write_string(out, value);
            uint8_t has_expiration = expires_at.has_value() ? 1 : 0;
            util::write_int<uint8_t>(out, has_expiration);
            if (expires_at.has_value()) {
                util::write_int<uint64_t>(out, expires_at.value());
            }
            ++count;
        });

        // seekp (seek put) - go to specified write position
        // seekg (seek get) - go to specified read position
        // update our entry counts
        out.seekp(count_pos);
        util::write_int<uint64_t>(out, count);

        // flush to disk & verify success
        out.flush();
        if (!out.good()) {
            throw std::runtime_error("failed to write snapshot");
        }

        entry_count_ = count;
    }
    // rename temp to original
    std::filesystem::rename(temp_path, path_);
}

void Snapshot::load(
    std::function<void(std::string_view, std::string_view, util::ExpirationTime)> callback) {
    std::ifstream in(path_, std::ios::binary);
    if (!in.is_open()) {
        return;
    }

    // verify header
    if (!validate_header(in)) {
        throw std::runtime_error("Invalid snapshot file: bad header");
    }

    uint64_t count;
    if (!util::read_int<uint64_t>(in, count)) {
        throw std::runtime_error("corrupt snapshot file");
    }

    std::string key;
    std::string value;

    // get each entry and pass to callback. store will insert to map
    for (uint64_t i = 0; i < count; ++i) {
        if (!util::read_string(in, key) || !util::read_string(in, value)) {
            throw std::runtime_error("corrupted snapshot file");
        }
        uint8_t has_expiration;
        if (!util::read_int<uint8_t>(in, has_expiration)) {
            throw std::runtime_error("corrupted snapshot file");
        }

        util::ExpirationTime expires_at = std::nullopt;
        if (has_expiration != 0) {
            int64_t expires_at_ms;
            if (!util::read_int<int64_t>(in, expires_at_ms)) {
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

bool Snapshot::validate_header(std::ifstream& in) {
    uint32_t magic;
    if (!util::read_int<uint32_t>(in, magic) || magic != kMagic) {
        return false;
    }

    uint32_t version;
    if (!util::read_int<uint32_t>(in, version) || version != kVersion) {
        return false;
    }
    return true;
}

}  // namespace kvstore::core