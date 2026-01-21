#include "kvstore/core/disk_store.hpp"
#include "kvstore/util/binary_io.hpp"

#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>

namespace kvstore::core {

namespace util = kvstore::util;

namespace {

constexpr uint32_t kMagic = 0x4B564453; //"KVDS"
constexpr uint32_t kVersion = 1;
constexpr uint8_t kEntryRegular = 0;
constexpr uint8_t kEntryTombstone = 1;

} //namespace

struct IndexEntry {
    uint64_t offset;
    uint32_t value_size;
    std::optional<util::TimePoint> expires_at;
    bool is_tombstone;
};

class DiskStore::Impl {
public:
    explicit Impl(const DiskStoreOptions& options)
        : options_(options), clock_(options.clock) 
    {
        std::filesystem::create_directories(options_.data_dir);
        data_path_ = options_.data_dir / "data.kvds";

        bool file_exists = std::filesystem::exists(data_path_);
        
        data_file_.open(data_path_, std::ios::binary | std::ios::in | std::ios::out);
        if(!data_file_.is_open()) {
            data_file_.open(data_path_, std::ios::binary | std::ios::out);
            data_file_.close();
            data_file_.open(data_path_, std::ios::binary | std::ios::in | std::ios::out);
        }

        if(!data_file_.is_open()) {
            throw std::runtime_error("failed to open data file: " + data_path_.string());
        }

        if(!file_exists || std::filesystem::file_size(data_path_) == 0) {
            util::write_uint32(data_file_, kMagic);
            util::write_uint32(data_file_, kVersion);
            data_file_.flush();
        } else {
            load_index();
        }
    }

    void put(std::string_view key, std::string_view value) {}
    void put(std::string_view key, std::string_view value, util::Duration ttl) {}
    [[nodiscard]] std::optional<std::string> get(std::string_view key) {}
    [[nodiscard]] bool remove(std::string_view key) {}
    [[nodiscard]] bool contains(std::string_view key) {}
    [[nodiscard]] std::size_t size() const noexcept {}
    [[nodiscard]] bool empty() const noexcept {}

    void clear() {}
    void compact() {}
private:
    void load_index() {}
    void append_entry(std::string_view key, std::string_view value, util::ExpirationTime expires_at_ms, bool is_tombstone){}
    [[nodiscard]] std::string read_value(const IndexEntry& entry) {}
    [[nodiscard]] bool is_expired(const IndexEntry& entry) const {}
    void maybe_compact() {}

    DiskStoreOptions options_;
    std::shared_ptr<util::Clock> clock_;

    std::filesystem::path data_path_;
    std::fstream data_file_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, IndexEntry> index_;
    std::size_t tombstone_count_ = 0;
    std::size_t entry_count_ = 0;
};

//PIMPL INTERFACE ---------------------------------------------------------------------------

DiskStore::DiskStore(const DiskStoreOptions& options)
    : impl_(std::make_unique<Impl>(options)) {}
DiskStore::~DiskStore() = default;
DiskStore::DiskStore(DiskStore&&) noexcept = default;
DiskStore& DiskStore::operator=(DiskStore&&) noexcept = default;
void DiskStore::put(std::string_view key, std::string_view value) {
    impl_->put(key, value);
}
void DiskStore::put(std::string_view key, std::string_view value, util::Duration ttl) {
    impl_->put(key, value, ttl);
}
std::optional<std::string> DiskStore::get(std::string_view key) {
    return impl_->get(key);
}
bool DiskStore::remove(std::string_view key) {
    return impl_->remove(key);
}
bool DiskStore::contains(std::string_view key) {
    return impl_->contains(key);
}
std::size_t DiskStore::size() const noexcept {
    return impl_->size();
}
bool DiskStore::empty() const noexcept {
    return impl_->empty();
}
void DiskStore::clear() {
    impl_->clear();
}
void DiskStore::compact() {
    impl_->compact();
}

} //kvstore::util