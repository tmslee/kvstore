#include "kvstore/core/disk_store.hpp"

#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>

#include "kvstore/util/binary_io.hpp"

namespace kvstore::core {

namespace util = kvstore::util;

namespace {

constexpr uint32_t kMagic = 0x4B564453;  //"KVDS"
constexpr uint32_t kVersion = 1;
constexpr uint8_t kEntryRegular = 0;
constexpr uint8_t kEntryTombstone = 1;

}  // namespace

struct IndexEntry {
    uint64_t offset;
    uint32_t value_size;
    std::optional<util::TimePoint> expires_at;
    bool is_tombstone;
};

// TODO: implement background compaction?

class DiskStore::Impl {
   public:
    explicit Impl(const DiskStoreOptions& options) : options_(options), clock_(options.clock) {
        std::filesystem::create_directories(options_.data_dir);
        data_path_ = options_.data_dir / "data.kvds";

        bool file_exists = std::filesystem::exists(data_path_);

        // open file for read+write
        data_file_.open(data_path_, std::ios::binary | std::ios::in | std::ios::out);
        if (!data_file_.is_open()) {
            data_file_.open(data_path_, std::ios::binary | std::ios::out);
            data_file_.close();
            data_file_.open(data_path_, std::ios::binary | std::ios::in | std::ios::out);
        }

        if (!data_file_.is_open()) {
            throw std::runtime_error("failed to open data file: " + data_path_.string());
        }

        // write header if new file. existing file - rebuild index by reading entries
        if (!file_exists || std::filesystem::file_size(data_path_) == 0) {
            util::write_uint32(data_file_, kMagic);
            util::write_uint32(data_file_, kVersion);
            data_file_.flush();
        } else {
            load_index();
        }
    }

    void put(std::string_view key, std::string_view value) {
        bool should_compact = false;
        {
            std::unique_lock lock(mutex_);
            append_entry(key, value, std::nullopt, false);
            should_compact = (tombstone_count_ >= options_.compaction_threshold);
        }
        if (should_compact) {
            try_auto_compact();
        }
    }

    void put(std::string_view key, std::string_view value, util::Duration ttl) {
        bool should_compact = false;
        {
            std::unique_lock lock(mutex_);
            auto expires_at = clock_->now() + ttl;
            append_entry(key, value, util::to_epoch_ms(expires_at), false);
            should_compact = (tombstone_count_ >= options_.compaction_threshold);
        }
        if (should_compact) {
            try_auto_compact();
        }
    }

    // design decision: we dont try to compact at get when we lazy delete an expired entry to keep
    // reads fast.
    [[nodiscard]] std::optional<std::string> get(std::string_view key) {
        std::unique_lock lock(mutex_);

        auto it = index_.find(std::string(key));
        if (it == index_.end()) {
            return std::nullopt;
        }

        if (is_expired(it->second)) {
            append_entry(key, "", std::nullopt, true);
            return std::nullopt;
        }

        return read_value(it->second);
    }

    [[nodiscard]] bool remove(std::string_view key) {
        bool should_compact = false;
        bool removed = false;
        {
            std::unique_lock lock(mutex_);

            auto it = index_.find(std::string(key));
            if (it == index_.end()) {
                return false;
            }

            append_entry(key, "", std::nullopt, true);
            removed = true;
            should_compact = (tombstone_count_ >= options_.compaction_threshold);
        }
        if (should_compact) {
            try_auto_compact();
        }
        return removed;
    }

    // design decision: we dont try to compact at contains when we lazy delete an expired entry to
    // keep reads fast.
    [[nodiscard]] bool contains(std::string_view key) {
        std::unique_lock lock(mutex_);

        auto it = index_.find(std::string(key));
        if (it == index_.end()) {
            return false;
        }

        if (is_expired(it->second)) {
            append_entry(key, "", std::nullopt, true);
            return false;
        }

        return true;
    }

    [[nodiscard]] std::size_t size() const {
        std::shared_lock lock(mutex_);
        return entry_count_;
    }

    [[nodiscard]] bool empty() const {
        std::shared_lock lock(mutex_);
        return entry_count_ == 0;
    }

    void clear() {
        std::unique_lock lock(mutex_);

        data_file_.close();

        data_file_.open(data_path_, std::ios::binary | std::ios::out | std::ios::trunc);
        util::write_uint32(data_file_, kMagic);
        util::write_uint32(data_file_, kVersion);
        data_file_.flush();
        data_file_.close();

        data_file_.open(data_path_, std::ios::binary | std::ios::in | std::ios::out);

        index_.clear();
        tombstone_count_ = 0;
        entry_count_ = 0;
    }

    void flush() {
        compact();
    }

    void compact() {
        std::unique_lock lock(mutex_);
        do_compact();
    }

   private:
    void load_index() {
        // go to beginning
        data_file_.seekg(0);

        // header check
        uint32_t magic = util::read_uint32(data_file_);
        if (magic != kMagic) {
            throw std::runtime_error("invalid data file: bad magic");
        }
        uint32_t version = util::read_uint32(data_file_);
        if (version != kVersion) {
            throw std::runtime_error("unsupported data file version: " + std::to_string(version));
        }

        // read every entry
        while (data_file_.peek() != EOF) {
            // keep current offset
            uint64_t offset = data_file_.tellg();

            // fetch type, key, value, expiration time
            uint8_t entry_type = util::read_uint8(data_file_);
            if (!data_file_.good()) {
                break;
            }
            std::string key;
            if (!util::read_string(data_file_, key)) {
                break;
            }
            std::string value;
            if (!util::read_string(data_file_, value)) {
                break;
            }
            uint8_t has_expiration = util::read_uint8(data_file_);
            if (!data_file_.good()) {
                break;
            }
            std::optional<util::TimePoint> expires_at = std::nullopt;
            if (has_expiration != 0) {
                int64_t expires_at_ms = util::read_int64(data_file_);
                if (!data_file_.good()) {
                    break;
                }
                expires_at = util::from_epoch_ms(expires_at_ms);
            }
            bool is_tombstone = (entry_type == kEntryTombstone);

            // if tombstone, remove from index. else add/update in index
            if (is_tombstone) {
                auto it = index_.find(key);
                if (it != index_.end()) {
                    index_.erase(it);
                    --entry_count_;
                }
                ++tombstone_count_;
            } else {
                IndexEntry entry{offset, static_cast<uint32_t>(value.size()), expires_at, false};
                auto it = index_.find(key);
                if (it != index_.end()) {
                    it->second = entry;
                } else {
                    index_[key] = entry;
                    ++entry_count_;
                }
            }
        }

        data_file_.clear();
    }

    void append_entry(std::string_view key, std::string_view value,
                      util::ExpirationTime expires_at_ms, bool is_tombstone) {
        // write to end of the file (append)
        data_file_.seekp(0, std::ios::end);
        uint64_t offset = data_file_.tellp();

        // write entry
        uint8_t entry_type = is_tombstone ? kEntryTombstone : kEntryRegular;
        util::write_uint8(data_file_, entry_type);
        util::write_string(data_file_, key);
        util::write_string(data_file_, value);

        uint8_t has_expiration = expires_at_ms.has_value() ? 1 : 0;
        util::write_uint8(data_file_, has_expiration);
        if (expires_at_ms.has_value()) {
            util::write_int64(data_file_, expires_at_ms.value());
        }

        data_file_.flush();

        // update index
        if (is_tombstone) {
            auto it = index_.find(std::string(key));
            if (it != index_.end()) {
                index_.erase(it);
                --entry_count_;
            }
            ++tombstone_count_;
        } else {
            std::optional<util::TimePoint> expires_at = std::nullopt;
            if (expires_at_ms.has_value()) {
                expires_at = util::from_epoch_ms(expires_at_ms.value());
            }

            IndexEntry entry{offset, static_cast<uint32_t>(value.size()), expires_at, false};

            auto it = index_.find(std::string(key));
            if (it != index_.end()) {
                it->second = entry;
            } else {
                index_[std::string(key)] = entry;
                ++entry_count_;
            }
        }
    }

    [[nodiscard]] std::string read_value(const IndexEntry& entry) {
        data_file_.seekg(entry.offset);

        util::read_uint8(data_file_);

        std::string key;
        util::read_string(data_file_, key);
        std::string value;
        util::read_string(data_file_, value);

        return value;
    }

    [[nodiscard]] bool is_expired(const IndexEntry& entry) const {
        if (!entry.expires_at.has_value()) {
            return false;
        }
        return clock_->now() >= entry.expires_at.value();
    }

    void try_auto_compact() {
        std::unique_lock lock(mutex_);
        if (tombstone_count_ >= options_.compaction_threshold) {
            do_compact();
        }
    }

    void do_compact() {
        // compact grabs entries from our current index and builds a new data file with it.
        // this just removes all the tombstones that might be present in our old data file
        std::filesystem::path temp_path = data_path_.string() + ".tmp";
        {
            std::ofstream temp_file(temp_path, std::ios::binary);
            if (!temp_file.is_open()) {
                throw std::runtime_error("failed to open temp file for compaction");
            }
            util::write_uint32(temp_file, kMagic);
            util::write_uint32(temp_file, kVersion);

            std::unordered_map<std::string, IndexEntry> new_index;

            for (auto& [key, entry] : index_) {
                if (is_expired(entry)) {
                    continue;
                }

                uint64_t new_offset = temp_file.tellp();

                std::string value = read_value(entry);

                util::write_uint8(temp_file, kEntryRegular);
                util::write_string(temp_file, key);
                util::write_string(temp_file, value);

                uint8_t has_expiration = entry.expires_at.has_value() ? 1 : 0;
                util::write_uint8(temp_file, has_expiration);
                if (entry.expires_at.has_value()) {
                    util::write_int64(temp_file, util::to_epoch_ms(entry.expires_at.value()));
                }

                new_index[key] = IndexEntry{new_offset, static_cast<uint32_t>(value.size()),
                                            entry.expires_at, false};
            }
            temp_file.flush();
        }
        data_file_.close();
        std::filesystem::rename(temp_path, data_path_);
        data_file_.open(data_path_, std::ios::binary | std::ios::in | std::ios::out);

        index_.clear();
        load_index();
        tombstone_count_ = 0;
    }

    DiskStoreOptions options_;
    std::shared_ptr<util::Clock> clock_;

    std::filesystem::path data_path_;
    std::fstream data_file_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, IndexEntry> index_;
    std::size_t tombstone_count_ = 0;
    std::size_t entry_count_ = 0;
};

// PIMPL INTERFACE ---------------------------------------------------------------------------

DiskStore::DiskStore(const DiskStoreOptions& options) : impl_(std::make_unique<Impl>(options)) {}
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
std::size_t DiskStore::size() const {
    return impl_->size();
}
bool DiskStore::empty() const {
    return impl_->empty();
}
void DiskStore::clear() {
    impl_->clear();
}
void DiskStore::flush() {
    impl_->flush();
}
void DiskStore::compact() {
    impl_->compact();
}

}  // namespace kvstore::core