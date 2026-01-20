#include "kvstore/core/store.hpp"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "kvstore/core/wal.hpp"

namespace kvstore::core {

class Store::Impl {
   public:
    Impl() = default;
    explicit Impl(const StoreOptions& options) {
        if (options.persistence_path.has_value()) {
            wal_ = std::make_unique<WriteAheadLog>(options.persistence_path.value());
            recover();
        }
    }

    void put(std::string_view key, std::string_view value) {
        std::unique_lock lock(mutex_);
        if (wal_) {
            wal_->log_put(key, value);
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
        }
        data_.clear();
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

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
    std::unique_ptr<WriteAheadLog> wal_;
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

}  // namespace kvstore::core