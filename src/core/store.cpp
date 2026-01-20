#include "kvstore/core/store.hpp"

#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

#include "kvstore/core/snapshot.hpp"
#include "kvstore/core/wal.hpp"

namespace kvstore::core {

namespace {
    int64_t to_epoch_ms(TimePoint tp) {
        auto duration = tp.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }

    TimePoint from_epoch_ms(int64_t ms) {
        return TimePoint(std::chrono::milliseconds(ms));
    }

    
} //namespace

struct Entry {
    std::string value;
    std::optional<util::TimePoint> expires_at = std::nullopt;
};

class Store::Impl {
   public:
    Impl() : clock_(std::make_shared<util::SystemClock>()) {}

    explicit Impl(const StoreOptions& options) : options_(options), clock_(options.clock) {
        // IMPORTANT: load snapshot first THEN WAL
        if (options_.snapshot_path.has_value()) {
            snapshot_ = std::make_unique<Snapshot>(options_.snapshot_path.value());
            if (snapshot_->exists()) {
                snapshot_->load([this](std::string_view key, std::string_view value, ExpirationTime expires_at_ms) {
                    std::optional<TimePoint> expires_at = std::nullopt;
                    if(expires_at_ms.has_value()) {
                        expires_at = from_epoch_ms(expires_at_ms.value());
                    }
                    if(!expires_at.has_value() || expires_at.value() > clock_->now()) {
                        data_[std::string(key)] = Entry{std::string(value), expires_at};
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
        std::unique_lock lock(mutex_);
        if (wal_) {
            wal_->log_put(key, value);
            ++wal_entries_since_snapshot_;
            maybe_snapshot();
        }
        data_[std::string(key)] = Entry{std::string(value), std::nullopt};
    }

    void put(std::string_view key, std::string_view value, util::Duration ttl) {
        std::unique_lock lock(mutex_);
        auto expires_at = clock_->now() + ttl;
        if (wal_) {
            wal_->log_put_with_ttl(key, value, to_epoch_ms(expires_at));
            ++wal_entries_since_snapshot_;
            maybe_snapshot();
        }
        data_[std::string(key)] = Entry{std::string(value), expires_at};
    }

    [[nodiscard]] std::optional<std::string> get(std::string_view key) {
        std::unique_lock lock(mutex_);
        auto it = data_.find(std::string(key));
        if (it == data_.end()) {
            return std::nullopt;
        }
        if (is_expired(it->second)) {
            data_.erase(it);
            return std::nullopt;
        }
        return it->second.value;
    }

    [[nodiscard]] bool remove(std::string_view key) {
        std::unique_lock lock(mutex_);
        if (wal_) {
            wal_->log_remove(key);
            ++wal_entries_since_snapshot_;
            maybe_snapshot();
        }
        return data_.erase(std::string(key)) > 0;
    }

    [[nodiscard]] bool contains(std::string_view key) {
        std::unique_lock lock(mutex_);
        auto it = data_.find(std::string(key));
        if (it == data_.end()) {
            return false;
        }
        if (is_expired(it->second)) {
            data_.erase(it);
            return false;
        }
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        std::shared_lock lock(mutex_);
        return data_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::shared_lock lock(mutex_);
        return data_.empty();
    }

    void clear() noexcept {
        std::unique_lock lock(mutex_);
        if (wal_) {
            wal_->log_clear();
            ++wal_entries_since_snapshot_;
            maybe_snapshot();
        }
        data_.clear();
    }

    void snapshot() {
        std::unique_lock lock(mutex_);
        do_snapshot();
    }

    void cleanup_expired() {
        std::unique_lock lock(mutex_);
        auto now = clock_->now();
        for (auto it = data_.begin(); it != data_.end();) {
            if (it->second.expires_at.has_value() && it->second.expires_at.value() <= now) {
                it = data_.erase(it);
            } else {
                ++it;
            }
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
        wal_->replay([this](EntryType type, std::string_view key, std::string_view value, ExpirationTime expires_at_ms) {
            switch (type) {
                case EntryType::Put:
                    data_[std::string(key)] = Entry{std::string(value), std::nullopt};
                    break;
                case EntryType::PutWithTTL:
                    auto expires_at = from_epoch_ms(expires_at_ms.value());
                    if(expires_at > clock_->now()) {
                        data_[std::string(key)] = Entry{std::string(value), expires_at};
                    }
                    break;
                case EntryType::Remove:
                    data_.erase(std::string(key));
                    break;
                case EntryType::Clear:
                    data_.clear();
                    break;
            }
        });
    }

    void maybe_snapshot() {
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
                if(!is_expired(entry)) {
                    ExpirationTime expires_at_ms = std::nullopt;
                    if(entry.expires_at.has_value()) {
                        expires_at_ms = to_epoch_ms(entry.expires_at.value());
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
    std::unique_ptr<WriteAheadLog> wal_;
    std::unique_ptr<Snapshot> snapshot_;
    std::size_t wal_entries_since_snapshot_ = 0;
};

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

std::size_t Store::size() const noexcept {
    return impl_->size();
}

bool Store::empty() const noexcept {
    return impl_->empty();
}

void Store::clear() noexcept {
    impl_->clear();
}

void Store::snapshot() {
    impl_->snapshot();
}

void Store::cleanup_expired() {
    impl_->cleanup_expired();
}

}  // namespace kvstore::core