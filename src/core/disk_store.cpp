#include "kvstore/core/disk_store.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

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

        // open file for read+write, create if doesn't exist
        fd_ = open(data_path_.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd_ < 0) {
            throw std::runtime_error("failed to open data file: " + data_path_.string());
        }

        // write header if new file. existing file - rebuild index by reading entries
        if (!file_exists || std::filesystem::file_size(data_path_) == 0) {
            util::write_int_fd<uint32_t>(fd_, kMagic);
            util::write_int_fd<uint32_t>(fd_, kVersion);
            fsync(fd_);
        } else {
            load_index();
        }
    }

    ~Impl() {
        if (fd_ >= 0) {
            close(fd_);
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

    // unique_lock required because read_value_from_fd() uses lseek on shared fd
    [[nodiscard]] std::optional<std::string> get(std::string_view key) {
        std::unique_lock lock(mutex_);

        auto it = index_.find(std::string(key));
        if (it == index_.end() || is_expired(it->second)) {
            return std::nullopt;  // don't write tombstone, let cleanup_expired() handle it
        }

        return read_value_from_fd(fd_, it->second.offset);
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

    [[nodiscard]] bool contains(std::string_view key) {
        std::shared_lock lock(mutex_);  // shared lock - reads don't modify data

        auto it = index_.find(std::string(key));
        if (it == index_.end() || is_expired(it->second)) {
            return false;  // don't write tombstone, let cleanup_expired() handle it
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

        // close current fd
        if (fd_ >= 0) {
            close(fd_);
        }

        // reopen with O_TRUNC to delete all content
        fd_ = open(data_path_.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd_ < 0) {
            throw std::runtime_error("failed to clear data file: " + data_path_.string());
        }

        util::write_int_fd<uint32_t>(fd_, kMagic);
        util::write_int_fd<uint32_t>(fd_, kVersion);
        fsync(fd_);

        index_.clear();
        tombstone_count_ = 0;
        entry_count_ = 0;
    }

    void flush() {
        compact();
    }

    void cleanup_expired() {
        bool should_compact = false;
        {
            std::unique_lock lock(mutex_);

            // collect expired keys first to avoid modifying index while iterating
            std::vector<std::string> expired_keys;
            for (const auto& [key, entry] : index_) {
                if (is_expired(entry)) {
                    expired_keys.push_back(key);
                }
            }

            // write tombstones for expired entries
            for (const auto& key : expired_keys) {
                append_entry(key, "", std::nullopt, true);
            }

            should_compact = (tombstone_count_ >= options_.compaction_threshold);
        }
        if (should_compact) {
            try_auto_compact();
        }
    }

    void compact() {
        std::unique_lock lock(mutex_);
        do_compact_unlocked(lock);
    }

   private:
    void load_index() {
        // go to beginning
        lseek(fd_, 0, SEEK_SET);

        // header check
        if (!validate_header()) {
            throw std::runtime_error("Invalid data file: bad header");
        }

        // get file size to know when we've reached the end
        off_t file_size = lseek(fd_, 0, SEEK_END);
        lseek(fd_, 8, SEEK_SET);  // skip header (magic + version = 8 bytes)

        // read every entry
        while (lseek(fd_, 0, SEEK_CUR) < file_size) {
            // keep current offset
            uint64_t offset = lseek(fd_, 0, SEEK_CUR);

            // fetch type, key, value, expiration time
            uint8_t entry_type;
            if (!util::read_int_fd<uint8_t>(fd_, entry_type)) {
                break;
            }
            std::string key;
            if (!util::read_string_fd(fd_, key)) {
                break;
            }
            std::string value;
            if (!util::read_string_fd(fd_, value)) {
                break;
            }
            uint8_t has_expiration;
            if (!util::read_int_fd<uint8_t>(fd_, has_expiration)) {
                break;
            }

            std::optional<util::TimePoint> expires_at = std::nullopt;
            if (has_expiration != 0) {
                uint64_t expires_at_ms;
                if (!util::read_int_fd<uint64_t>(fd_, expires_at_ms)) {
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
    }

    void append_entry(std::string_view key, std::string_view value,
                      util::ExpirationTime expires_at_ms, bool is_tombstone) {
        // write to end of the file (append)
        uint64_t offset = lseek(fd_, 0, SEEK_END);

        // write entry
        uint8_t entry_type = is_tombstone ? kEntryTombstone : kEntryRegular;
        util::write_int_fd<uint8_t>(fd_, entry_type);
        util::write_string_fd(fd_, key);
        util::write_string_fd(fd_, value);

        uint8_t has_expiration = expires_at_ms.has_value() ? 1 : 0;
        util::write_int_fd<uint8_t>(fd_, has_expiration);
        if (expires_at_ms.has_value()) {
            util::write_int_fd<uint64_t>(fd_, expires_at_ms.value());
        }

        // no fsync here for performance - caller can use flush() for durability

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

    // Read value from any fd at a specific offset (for concurrent reads during compaction)
    [[nodiscard]] static std::string read_value_from_fd(int fd, uint64_t offset) {
        lseek(fd, offset, SEEK_SET);
        uint8_t entry_type;
        util::read_int_fd<uint8_t>(fd, entry_type);
        std::string key;
        util::read_string_fd(fd, key);
        std::string value;
        util::read_string_fd(fd, value);
        return value;
    }

    [[nodiscard]] bool is_expired(const IndexEntry& entry) const {
        if (!entry.expires_at.has_value()) {
            return false;
        }
        return clock_->now() >= entry.expires_at.value();
    }

    void try_auto_compact() {
        // Check threshold under lock, but do_compact releases lock during I/O
        std::unique_lock lock(mutex_);
        if (tombstone_count_ >= options_.compaction_threshold) {
            do_compact_unlocked(lock);
        }
    }

    // Called with lock already held - releases during I/O phase
    void do_compact_unlocked(std::unique_lock<std::shared_mutex>& lock) {
        // Phase 1 (lock held): Snapshot index and record file position
        std::vector<std::pair<std::string, IndexEntry>> snapshot;
        snapshot.reserve(index_.size());
        for (const auto& [key, entry] : index_) {
            if (!is_expired(entry)) {
                snapshot.emplace_back(key, entry);
            }
        }
        uint64_t compaction_start_offset = lseek(fd_, 0, SEEK_END);

        // Release lock during I/O-heavy phase
        lock.unlock();

        // Phase 2 (no lock): Write snapshot to temp file using separate read fd
        std::filesystem::path temp_path = data_path_.string() + ".tmp";
        int temp_fd = open(temp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (temp_fd < 0) {
            lock.lock();  // Re-acquire before throwing
            throw std::runtime_error("failed to open temp file for compaction");
        }

        int read_fd = open(data_path_.c_str(), O_RDONLY);
        if (read_fd < 0) {
            close(temp_fd);
            std::filesystem::remove(temp_path);
            lock.lock();
            throw std::runtime_error("failed to open read fd for compaction");
        }

        // Write header
        util::write_int_fd<uint32_t>(temp_fd, kMagic);
        util::write_int_fd<uint32_t>(temp_fd, kVersion);

        // Write all snapshotted entries
        for (const auto& [key, entry] : snapshot) {
            std::string value = read_value_from_fd(read_fd, entry.offset);

            util::write_int_fd<uint8_t>(temp_fd, kEntryRegular);
            util::write_string_fd(temp_fd, key);
            util::write_string_fd(temp_fd, value);

            uint8_t has_expiration = entry.expires_at.has_value() ? 1 : 0;
            util::write_int_fd<uint8_t>(temp_fd, has_expiration);
            if (entry.expires_at.has_value()) {
                util::write_int_fd<uint64_t>(temp_fd, util::to_epoch_ms(entry.expires_at.value()));
            }
        }

        close(read_fd);

        // Phase 3 (lock held): Merge new writes, finalize, and swap
        lock.lock();

        // Read any entries written since compaction started and append to temp
        uint64_t current_end = lseek(fd_, 0, SEEK_END);
        if (current_end > compaction_start_offset) {
            lseek(fd_, compaction_start_offset, SEEK_SET);

            while (lseek(fd_, 0, SEEK_CUR) < static_cast<off_t>(current_end)) {
                uint8_t entry_type;
                if (!util::read_int_fd<uint8_t>(fd_, entry_type)) {
                    break;
                }
                std::string key;
                if (!util::read_string_fd(fd_, key)) {
                    break;
                }
                std::string value;
                if (!util::read_string_fd(fd_, value)) {
                    break;
                }
                uint8_t has_expiration;
                if (!util::read_int_fd<uint8_t>(fd_, has_expiration)) {
                    break;
                }
                std::optional<int64_t> expires_at_ms;
                if (has_expiration != 0) {
                    int64_t exp;
                    if (!util::read_int_fd<int64_t>(fd_, exp)) {
                        break;
                    }
                    expires_at_ms = exp;
                }

                // Write to temp file (including tombstones - they represent recent deletes)
                util::write_int_fd<uint8_t>(temp_fd, entry_type);
                util::write_string_fd(temp_fd, key);
                util::write_string_fd(temp_fd, value);
                util::write_int_fd<uint8_t>(temp_fd, has_expiration);
                if (expires_at_ms.has_value()) {
                    util::write_int_fd<uint64_t>(temp_fd, expires_at_ms.value());
                }
            }
        }

        // fsync temp file before rename
        fsync(temp_fd);
        close(temp_fd);

        // close current fd before rename
        close(fd_);

        // rename temp to final (atomic on POSIX)
        std::filesystem::rename(temp_path, data_path_);

        // fsync directory for durable rename
        std::filesystem::path dir_path = data_path_.parent_path();
        if (dir_path.empty()) {
            dir_path = ".";
        }
        int dir_fd = open(dir_path.c_str(), O_RDONLY);
        if (dir_fd >= 0) {
            fsync(dir_fd);
            close(dir_fd);
        }

        // reopen the data file
        fd_ = open(data_path_.c_str(), O_RDWR, 0644);

        index_.clear();
        load_index();
        // Note: don't reset tombstone_count_ - load_index() sets it correctly
        // (new writes during Phase 2 may include tombstones)
    }

    bool validate_header() {
        uint32_t magic;
        if (!util::read_int_fd<uint32_t>(fd_, magic) || magic != kMagic) {
            return false;
        }

        uint32_t version;
        if (!util::read_int_fd<uint32_t>(fd_, version) || version != kVersion) {
            return false;
        }
        return true;
    }

    DiskStoreOptions options_;
    std::shared_ptr<util::Clock> clock_;

    std::filesystem::path data_path_;
    int fd_ = -1;

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
void DiskStore::cleanup_expired() {
    impl_->cleanup_expired();
}
void DiskStore::compact() {
    impl_->compact();
}

}  // namespace kvstore::core
