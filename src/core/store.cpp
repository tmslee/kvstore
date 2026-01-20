#include "kvstore/core/store.hpp"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "kvstore/core/snapshot.hpp"
#include "kvstore/core/wal.hpp"

namespace kvstore::core {

class Store::Impl {
   public:
    Impl() = default;

    explicit Impl(const StoreOptions& options) : options_(options) {
        // IMPORTANT: load snapshot first THEN WAL
        if (options_.snapshot_path.has_value()) {
            snapshot_ = std::make_unique<Snapshot>(options_.snapshot_path.value());
            if (snapshot_->exists()) {
                snapshot_->load([this](std::string_view key, std::string_view value) {
                    data_.insert_or_assign(std::string(key), std::string(value));
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
        data_.insert_or_assign(std::string(key), std::string(value));
    }

    [[nodiscard]] std::optional<std::string> get(std::string_view key) const {
        std::shared_lock lock(mutex_);
        if (auto it = data_.find(std::string(key)); it != data_.end()) {
            return it->second;
        }
        return std::nullopt;
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

    [[nodiscard]] bool contains(std::string_view key) const {
        std::shared_lock lock(mutex_);
        return data_.contains(std::string(key));
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

   private:
    void recover() {
        wal_->replay([this](EntryType type, std::string_view key, std::string_view value) {
            switch (type) {
                case EntryType::Put:
                    data_.insert_or_assign(std::string(key), std::string(value));
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

        snapshot_->save([this](std::function<void(std::string_view, std::string_view)> emit) {
            for (const auto& [key, value] : data_) {
                emit(key, value);
            }
        });

        if (wal_) {
            wal_->truncate();
        }

        wal_entries_since_snapshot_ = 0;
    }

    StoreOptions options_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
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
std::optional<std::string> Store::get(std::string_view key) const {
    return impl_->get(key);
}

bool Store::remove(std::string_view key) {
    return impl_->remove(key);
}

bool Store::contains(std::string_view key) const {
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

}  // namespace kvstore::core