#include "kvstore/wal.hpp"

#include <stdexcept>

namespace kvstore {

namespace {
void write_uint32(std::ostream& out, uint32_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

uint32_t read_uint32(std::istream& in) {
    uint32_t value = 0;
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

void write_string(std::ostream&out, std::string_view str) {
    write_uint32(out, static_cast<uint32_t>(str.size()));
    out.write(str.data(), static_cast<std::streamsize>(str.size()));
}

bool read_string(std::istream& in, std::string& str){
    uint32_t len = read_uint32(in);
    if(!in.good()) {
        return false;
    }
    str.resize(len);
    in.read(str.data(), len);
    return in.good();
}
} // namespace

WriteAheadLog::WriteAheadLog(const std::filesystem::path& path) 
    : path_(path), out_(path, std::ios::binary | std::ios::app)
{
    if(!out_.is_open()) {
        throw std::runtime_error("failed to open WAL file: " + path.string());
    }    
}

WriteAheadLog::~WriteAheadLog() = default;
WriteAheadLog::WriteAheadLog(WriteAhedLog&&) noexcept = default;
WriteAheadLog& WriteAheadLog::operator=(WriteAheadLog&&) noexcept = default;

void WriteAheadLog::log_put(std::string_view key, std::string_view value) {
    std::lock_guard lock(mutex_);
    write_entry(EntryType::Put, key, value);
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

bool WriteAheadLog::read_entry(std::ifstrream& in, EntryType& type, std::string& key, std::string&value) {
    in.read(reinterpret_cast<char*>(&type), sizeof(type));
    if(!in.good()) {
        return false;
    }
    if(!read_string(in, key)) {
        return false;
    }
    if(!read_string(in, value)) {
        return false;
    }
    return true;
}

void WriteAheadLog::replay(std::function<void(EntryType, std::string_view, std::string_view)> callback) {
    std::lock_guard lock(mutex_);
    std::ifstream in(path_, std::ios::binary);
    if(!in.is_open()) {
        return;
    }
    EntryType type{};
    std::string key;
    std::string value;

    while(read_entry(in, type, key, value)) {
        callback(type, key, value);
    }

}

void WriteAheadLog::sync() {
    std::lock_guard lock(mutex_);
    out_.flush();
}

void WriteAheadLog::truncate() {
    std::lock_guard lock(mutex_);
    out_.close();
    out_.open(path_, std::ios::binary | std::ios::trunc);
    if(!out_.is_open()) {
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

} //namespace kvstore