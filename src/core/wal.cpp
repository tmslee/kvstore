#include "kvstore/core/wal.hpp"

#include <stdexcept>

namespace kvstore::core {

namespace {
void write_uint32(std::ostream& out, uint32_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

uint32_t read_uint32(std::istream& in) {
    uint32_t value = 0;
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

void write_int64(std::ostream& out, int64_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

int64_t read_int64(std::istream& in) {
    int64_t value = 0;
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

void write_string(std::ostream& out, std::string_view str) {
    write_uint32(out, static_cast<uint32_t>(str.size()));
    // need to cast str.size() (std::size_t is signed) to std::streamsize (unsigned)to avoid
    // warnings
    out.write(str.data(), static_cast<std::streamsize>(str.size()));
}

bool read_string(std::istream& in, std::string& str) {
    uint32_t len = read_uint32(in);
    if (!in.good()) {
        return false;
    }
    str.resize(len);
    in.read(str.data(), len);
    return in.good();
}
}  // namespace

// note: std::ios::binary | std::ios::app are both binary flags and are being OR'ed
// binary: dont translate newlines, write raw bytes exactly as given
// app: append mmode
WriteAheadLog::WriteAheadLog(const std::filesystem::path& path)
    : path_(path), out_(path, std::ios::binary | std::ios::app) {
    if (!out_.is_open()) {
        throw std::runtime_error("failed to open WAL file: " + path.string());
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
    out_.write(reinterpret_cast<const char*>(&type), sizeof(type));
    write_string(out_, key);
    write_string(out_, value);
    out_.flush();
}

void WriteAheadLog::write_entry_with_ttl(EntryType type, std::string_view key,
                                         std::string_view value, int64_t expires_at_ms) {
    out_.write(reinterpret_cast<const char*>(&type), sizeof(type));
    write_string(out_, key);
    write_string(out_, value);
    write_int64(out_, expires_at_ms);
    out_.flush();
}

bool WriteAheadLog::read_entry(std::ifstream& in, EntryType& type, std::string& key,
                               std::string& value, ExpirationTime& expires_at) {
    // we return bool instead of throwing because end of file is expected, not exceptional
    // not being able to read successfully is an expected pattern eventually
    in.read(reinterpret_cast<char*>(&type), sizeof(type));
    if (!in.good()) {
        return false;
    }
    if (!read_string(in, key)) {
        return false;
    }
    if (!read_string(in, value)) {
        return false;
    }
    if (type == EntryType::PutWithTTL) {
        expires_at = read_int64(in);
        if (!in.good()) {
            return false;
        }
    } else {
        expires_at = std::nullopt;
    }
    return true;
}

void WriteAheadLog::replay(
    std::function<void(EntryType, std::string_view, std::string_view, ExpirationTime)> callback) {
    std::lock_guard lock(mutex_);

    std::ifstream in(path_, std::ios::binary);
    if (!in.is_open()) {
        return;
    }

    EntryType type{};
    std::string key;
    std::string value;
    ExpirationTime expires_at;
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
}

std::filesystem::path WriteAheadLog::path() const {
    return path_;
}

std::size_t WriteAheadLog::size() const {
    std::lock_guard lock(mutex_);
    return std::filesystem::file_size(path_);
}

}  // namespace kvstore::core