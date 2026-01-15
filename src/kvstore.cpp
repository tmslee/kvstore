#include "kvstore/kvstore.hpp"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace kvstore {

class KVStore::Impl {
   public:
    void put(std::string_view key, std::string_view value) {
        std::unique_lock lock(mutex_);
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
        data_.clear();
    }

   private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
};

KVStore::KVStore() : impl_(std::make_unique<Impl>()) {}

KVStore::~KVStore() = default;

KVStore::KVStore(KVStore&&) noexcept = default;

KVStore& KVStore::operator=(KVStore&&) noexcept = default;

void KVStore::put(std::string_view key, std::string_view value) {
    impl_->put(key, value);
}
std::optional<std::string> KVStore::get(std::string_view key) const {
    return impl_->get(key);
}

bool KVStore::remove(std::string_view key) {
    return impl_->remove(key);
}

bool KVStore::contains(std::string_view key) const {
    return impl_->contains(key);
}

std::size_t KVStore::size() const noexcept {
    return impl_->size();
}

bool KVStore::empty() const noexcept {
    return impl_->empty();
}

void KVStore::clear() noexcept {
    impl_->clear();
}

}  // namespace kvstore