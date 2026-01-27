#include "kvstore/core/wal.hpp"

#include <stdexcept>

#include "kvstore/util/binary_io.hpp"

namespace kvstore::core {

namespace util = kvstore::util;

// note: std::ios::binary | std::ios::app are both binary flags and are being OR'ed
// binary: dont translate newlines, write raw bytes exactly as given
// app: append mmode
WriteAheadLog::WriteAheadLog(const std::filesystem::path& path) : path_(path) {
    bool file_exists = std::filesystem::exists(path_);
    out_.open(path_, std::ios::binary | std::ios::app);

    if (!out_.is_open()) {
        throw std::runtime_error("failed to open WAL file: " + path.string());
    }

    if (!file_exists || std::filesystem::file_size(path_) == 0) {
        write_header();
    }
}

WriteAheadLog::~WriteAheadLog() = default;
/*
    note: std::mutex cannot be moved/copied.
    keeping move ctor default makes compiler try to move each member, but mutex cannot be moved
    manually implement move ctor and assignment.
    mutex is jsut default constructed fresh in new object
*/
WriteAheadLog::WriteAheadLog(WriteAheadLog&& other) noexcept
    : path_(std::move(other.path_)), out_(std::move(other.out_)) {}

WriteAheadLog& WriteAheadLog::operator=(WriteAheadLog&& other) noexcept {
    if (this != &other) {
        path_ = std::move(other.path_);
        out_ = std::move(other.out_);
    }
    return *this;
}

void WriteAheadLog::write_header() {
    util::write_int<uint32_t>(out_, kMagic);
    util::write_int<uint32_t>(out_, kVersion);
    out_.flush();
}

bool WriteAheadLog::validate_header(std::ifstream& in) {
    uint32_t magic;
    if(!util::read_int<uint32_t>(in, magic) || magic != kMagic) {
        return false;
    }

    uint32_t version; 
    if(!util::read_int<uint32_t>(in, version) || version != kVersion) {
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
    util::write_int<uint8_t>(out_, static_cast<uint8_t>(type));
    util::write_string(out_, key);
    util::write_string(out_, value);
    out_.flush();
}

void WriteAheadLog::write_entry_with_ttl(EntryType type, std::string_view key,
                                         std::string_view value, int64_t expires_at_ms) {
    util::write_int<uint8_t>(out_, static_cast<uint8_t>(type));
    util::write_string(out_, key);
    util::write_string(out_, value);
    util::write_int<uint64_t>(out_, expires_at_ms);
    out_.flush();
}

bool WriteAheadLog::read_entry(std::ifstream& in, EntryType& type, std::string& key,
                               std::string& value, util::ExpirationTime& expires_at) {
    // we return bool instead of throwing because end of file is expected, not exceptional
    // not being able to read successfully is an expected pattern eventually
    uint8_t type_byte;
    if(!util::read_int<uint8_t>(in, type_byte)){
        return false;
    }
    
    type = static_cast<EntryType>(type_byte);
    if (!util::read_string(in, key)) {
        return false;
    }
    if (!util::read_string(in, value)) {
        return false;
    }
    if (type == EntryType::PutWithTTL) {
        int64_t expires_at_ms;
        if(!util::read_int<int64_t>(in, expires_at_ms)) {
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

    std::ifstream in(path_, std::ios::binary);
    if (!in.is_open()) {
        return;
    }

    if (!validate_header(in)) {
        throw std::runtime_error("Invalid WAL file: bad header");
    }

    EntryType type{};
    std::string key;
    std::string value;
    util::ExpirationTime expires_at;
    // try to read entry sequentially until end of file or failure
    while (read_entry(in, type, key, value, expires_at)) {
        callback(type, key, value, expires_at);
    }
}

void WriteAheadLog::sync() {
    std::lock_guard lock(mutex_);
    // flush forces buffered data to be written to disk immediately
    out_.flush();
}

void WriteAheadLog::truncate() {
    std::lock_guard lock(mutex_);
    out_.close();
    // trunc: truncate, delete all existing content
    out_.open(path_, std::ios::binary | std::ios::trunc);
    if (!out_.is_open()) {
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