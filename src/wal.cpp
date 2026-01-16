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

bool read_string(std::istream& in, std:;string& str){
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

}

void WriteAheadLog::log_remove(std::string_view key) {

}

void WriteAheadLog::log_clear() {

}

void WriteAheadLog::write_entry(EntryType type, std::string_view key, std::string_view value) {

}

bool WriteAheadLog::read_entry(std::ifstrream& in, EntryType& type, std::string& key, std::string&value) {

}

void WriteAheadLog::replay(std::function<void(EntryType, std::string_view, std::string_view)> callback) {

}

void WriteAheadLog::sync() {

}

void WriteAheadLog::truncate() {

}

std::filesystem::path WriteAheadLog::path() const {

}

std::size_t WriteAheadLog::size() const {

}

} //namespace kvstore