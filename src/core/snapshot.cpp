#include "kvstore/core/snapshot.hpp"

#include <fstream>
#include <stdexcept>

namespace kvstore::core {

namespace {

void write_uint64(std::ostream& out, uint64_t value) {}

uint64_t read_uint64(std::istream& in) {}

void write_uint32(std::ostream& out, uint32_t value) {}

uint32_t read_uint32(std::istream& in) {}

void write_string(std::ostream& out, std::string_view str) {}

bool read_string(std::istream& in, std::string& str) {}

constexpr uint32_t kMagic = 0x4B565353; // "KVSS"
constexpr uint32_t kVersion = 1;

} //namespace

Snapshot::Snapshot(const std::filesystem::path& path) : path_(path) {}

void Snapshot::save(const std::function<void(std::function<void(std::string_view, std::string_view)>)>& iterate){}

void Snapshot::load(std::function<void(std::string_view, std::string_view)> callback) {}

bool Snapshot::exists() const {}

std::filesystem::path Snapshot::path() const {}

std::size_t Snapshot::entry_count() const {}

} //namespace kvstore::core