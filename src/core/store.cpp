#include "kvstore/core/store.hpp"

#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <unordered_map>

#include "kvstore/core/snapshot.hpp"
#include "kvstore/core/wal.hpp"
#include "kvstore/util/types.hpp"

namespace kvstore::core {

namespace util = kvstore::util;

struct Entry {
    std::string value;
    std::optional<util::TimePoint> expires_at = std::nullopt;
};

// Min-heap for efficient expiration cleanup: (expiration_time, key)
// Uses lazy deletion - when popping, verify the key still exists with matching expiration
using ExpirationEntry = std::pair<util::TimePoint, std::string>;
struct ExpirationCompare {
    bool operator()(const ExpirationEntry& a, const ExpirationEntry& b) const {
        return a.first > b.first;  // min-heap: smallest expiration first
    }
};
using ExpirationQueue =
    std::priority_queue<ExpirationEntry, std::vector<ExpirationEntry>, ExpirationCompare>;

class Store::Impl {
   public:
    Impl() : clock_(std::make_shared<util::SystemClock>()) {}

    explicit Impl(const StoreOptions& options) : options_(options), clock_(options.clock) {
        // IMPORTANT: load snapshot first THEN WAL
        if (options_.snapshot_path.has_value()) {
            snapshot_ = std::make_unique<Snapshot>(options_.snapshot_path.value());
            if (snapshot_->exists()) {
                snapshot_->load([this](std::string_view key, std::string_view value,
                                       util::ExpirationTime expires_at_ms) {
                    std::optional<util::TimePoint> expires_at = std::nullopt;
                    if (expires_at_ms.has_value()) {
                        expires_at = util::from_epoch_ms(expires_at_ms.value());
                    }
                    if (!expires_at.has_value() || expires_at.value() > clock_->now()) {
                        data_[std::string(key)] = Entry{std::string(value), expires_at};
                        if (expires_at.has_value()) {
                            expiration_queue_.emplace(expires_at.value(), std::string(key));
                        }
                    }
                });
            }
        }

        if (options_.persistence_path.has_value()) {
            wal_ = std::make_unique<WriteAheadLog>(options_.persistence_path.value());
            recover();
        }
    }

    void put(std::string_view key, std::string_view value) {
        bool should_snapshot = false;
        {
            std::unique_lock lock(mutex_);
            if (wal_) {
                wal_->log_put(key, value);
                ++wal_entries_since_snapshot_;
                should_snapshot =
                    snapshot_ && (wal_entries_since_snapshot_ >= options_.snapshot_threshold);
            }
            data_[std::string(key)] = Entry{std::string(value), std::nullopt};
        }
        if (should_snapshot) {
            try_auto_snapshot();
        }
    }

    void put(std::string_view key, std::string_view value, util::Duration ttl) {
        bool should_snapshot = false;
        {
            std::unique_lock lock(mutex_);
            auto expires_at = clock_->now() + ttl;
            if (wal_) {
                wal_->log_put_with_ttl(key, value, util::to_epoch_ms(expires_at));
                ++wal_entries_since_snapshot_;
                should_snapshot =
                    snapshot_ && (wal_entries_since_snapshot_ >= options_.snapshot_threshold);
            }
            data_[std::string(key)] = Entry{std::string(value), expires_at};
            expiration_queue_.emplace(expires_at, std::string(key));
        }
        if (should_snapshot) {
            try_auto_snapshot();
        }
    }

    [[nodiscard]] std::optional<std::string> get(std::string_view key) {
        std::shared_lock lock(mutex_);  // shared lock - reads don't modify data
        auto it = data_.find(std::string(key));
        if (it == data_.end() || is_expired(it->second)) {
            return std::nullopt;  // don't erase, let cleanup_expired() handle it
        }
        return it->second.value;
    }

    [[nodiscard]] bool remove(std::string_view key) {
        bool should_snapshot = false;
        bool removed = false;
        {
            std::unique_lock lock(mutex_);
            if (wal_) {
                wal_->log_remove(key);
                ++wal_entries_since_snapshot_;
                should_snapshot =
                    snapshot_ && (wal_entries_since_snapshot_ >= options_.snapshot_threshold);
            }
            removed = data_.erase(std::string(key)) > 0;
        }
        if (should_snapshot) {
            try_auto_snapshot();
        }
        return removed;
    }

    [[nodiscard]] bool contains(std::string_view key) {
        std::shared_lock lock(mutex_);  // shared lock - reads don't modify data
        auto it = data_.find(std::string(key));
        if (it == data_.end() || is_expired(it->second)) {
            return false;  // don't erase, let cleanup_expired() handle it
        }
        return true;
    }

    [[nodiscard]] std::size_t size() const {
        std::shared_lock lock(mutex_);
        return data_.size();
    }

    [[nodiscard]] bool empty() const {
        std::shared_lock lock(mutex_);
        return data_.empty();
    }

    void clear() {
        bool should_snapshot = false;
        {
            std::unique_lock lock(mutex_);
            if (wal_) {
                wal_->log_clear();
                ++wal_entries_since_snapshot_;
                should_snapshot =
                    snapshot_ && (wal_entries_since_snapshot_ >= options_.snapshot_threshold);
            }
            data_.clear();
            expiration_queue_ = ExpirationQueue{};  // clear expiration queue
        }
        if (should_snapshot) {
            try_auto_snapshot();
        }
    }

    void flush() {
        snapshot();
    }

    void snapshot() {
        std::unique_lock lock(mutex_);
        do_snapshot();
    }

    void cleanup_expired() {
        std::unique_lock lock(mutex_);
        auto now = clock_->now();

        // Process expiration queue - O(k) where k = number of expired entries
        // Uses lazy deletion: verify key still exists with matching expiration
        while (!expiration_queue_.empty()) {
            const auto& [exp_time, key] = expiration_queue_.top();
            if (exp_time > now) {
                break;  // no more expired entries
            }

            // Check if this entry is still valid (not updated/deleted since queued)
            auto it = data_.find(key);
            if (it != data_.end() && it->second.expires_at.has_value() &&
                it->second.expires_at.value() == exp_time) {
                data_.erase(it);
            }
            expiration_queue_.pop();
        }
    }

   private:
    [[nodiscard]] bool is_expired(const Entry& entry) const {
        if (!entry.expires_at.has_value()) {
            return false;
        }
        return clock_->now() >= entry.expires_at.value();
    }

    void recover() {
        wal_->replay([this](EntryType type, std::string_view key, std::string_view value,
                            util::ExpirationTime expires_at_ms) {
            switch (type) {
                case EntryType::Put:
                    data_[std::string(key)] = Entry{std::string(value), std::nullopt};
                    break;
                case EntryType::PutWithTTL: {
                    auto expires_at = util::from_epoch_ms(expires_at_ms.value());
                    if (expires_at > clock_->now()) {
                        data_[std::string(key)] = Entry{std::string(value), expires_at};
                        expiration_queue_.emplace(expires_at, std::string(key));
                    }
                    break;
                }
                case EntryType::Remove:
                    data_.erase(std::string(key));
                    break;
                case EntryType::Clear:
                    data_.clear();
                    // clear expiration queue too
                    expiration_queue_ = ExpirationQueue{};
                    break;
            }
        });
    }

    // try_auto_snapshot does another state check under a lock to ensure no double snapshotting
    // across threads
    void try_auto_snapshot() {
        std::unique_lock lock(mutex_);
        if (snapshot_ && wal_entries_since_snapshot_ >= options_.snapshot_threshold) {
            do_snapshot();
        }
    }

    void do_snapshot() {
        if (!snapshot_) {
            return;
        }

        snapshot_->save([this](EntryEmitter emit) {
            for (const auto& [key, entry] : data_) {
                if (!is_expired(entry)) {
                    util::ExpirationTime expires_at_ms = std::nullopt;
                    if (entry.expires_at.has_value()) {
                        expires_at_ms = util::to_epoch_ms(entry.expires_at.value());
                    }
                    emit(key, entry.value, expires_at_ms);
                }
            }
        });

        if (wal_) {
            wal_->truncate();
        }

        wal_entries_since_snapshot_ = 0;
    }

    StoreOptions options_;
    std::shared_ptr<util::Clock> clock_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Entry> data_;
    ExpirationQueue expiration_queue_;  // min-heap for efficient cleanup
    std::unique_ptr<WriteAheadLog> wal_;
    std::unique_ptr<Snapshot> snapshot_;
    std::size_t wal_entries_since_snapshot_ = 0;
};

// PIMPL INTERFACE --------------------------------------------------------------------
Store::Store() : impl_(std::make_unique<Impl>()) {}
Store::Store(const StoreOptions& options) : impl_(std::make_unique<Impl>(options)) {}
Store::~Store() = default;
Store::Store(Store&&) noexcept = default;
Store& Store::operator=(Store&&) noexcept = default;
void Store::put(std::string_view key, std::string_view value) {
    impl_->put(key, value);
}
void Store::put(std::string_view key, std::string_view value, util::Duration ttl) {
    impl_->put(key, value, ttl);
}
std::optional<std::string> Store::get(std::string_view key) {
    return impl_->get(key);
}
bool Store::remove(std::string_view key) {
    return impl_->remove(key);
}
bool Store::contains(std::string_view key) {
    return impl_->contains(key);
}
std::size_t Store::size() const {
    return impl_->size();
}
bool Store::empty() const {
    return impl_->empty();
}
void Store::clear() {
    impl_->clear();
}
void Store::flush() {
    impl_->flush();
}
void Store::snapshot() {
    impl_->snapshot();
}
void Store::cleanup_expired() {
    impl_->cleanup_expired();
}

}  // namespace kvstore::core