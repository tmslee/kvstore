#include "kvstore/core/snapshot.hpp"

#include <fstream>
#include <stdexcept>

#include "kvstore/util/binary_io.hpp"

namespace kvstore::core {

using namespace kvstore::util;

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
        write_uint32(out, kMagic);
        write_uint32(out, kVersion);

        // tellp (tell put) - return current write position
        // tellg (tell get) - return current read position
        auto count_pos = out.tellp();
        // write placeholder for count (we dont know yet)
        write_uint64(out, 0);

        std::size_t count = 0;
        // call iterator & pass the lambda which is the entry emitter
        iterate([&out, &count](std::string_view key, std::string_view value,
                               ExpirationTime expires_at) {
            write_string(out, key);
            write_string(out, value);
            uint8_t has_expiration = expires_at.has_value() ? 1 : 0;
            write_uint8(out, has_expiration);
            if (expires_at.has_value()) {
                write_int64(out, expires_at.value());
            }
            ++count;
        });

        // seekp (seek put) - go to specified write position
        // seekg (seek get) - go to specified read position
        // update our entry counts
        out.seekp(count_pos);
        write_uint64(out, count);

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
    std::function<void(std::string_view, std::string_view, ExpirationTime)> callback) {
    std::ifstream in(path_, std::ios::binary);
    if (!in.is_open()) {
        return;
    }

    // verify header
    uint32_t magic = read_uint32(in);
    if (magic != kMagic) {
        throw std::runtime_error("invalid snapshot file: bad magic");
    }

    uint32_t version = read_uint32(in);
    if (version != kVersion) {
        throw std::runtime_error("unsupported snapshot version: " + std::to_string(version));
    }

    uint64_t count = read_uint64(in);

    std::string key;
    std::string value;

    // get each entry and pass to callback. store will insert to map
    for (uint64_t i = 0; i < count; ++i) {
        if (!read_string(in, key) || !read_string(in, value)) {
            throw std::runtime_error("corrupted snapshot file");
        }
        uint8_t has_expiration = read_uint8(in);
        if (!in.good()) {
            throw std::runtime_error("corrupted snapshot file");
        }

        ExpirationTime expires_at = std::nullopt;
        if (has_expiration != 0) {
            expires_at = read_int64(in);
            if (!in.good()) {
                throw std::runtime_error("corrupted snapshot file");
            }
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

}  // namespace kvstore::core