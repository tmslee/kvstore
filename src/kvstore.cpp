#include "kvstore/kvstore.hpp"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace kvstore {

class KVStore::Impl {
public:
    void put(std::string_view key, std::string_view value) {

    }

    [[nodiscard]] std::optional<std::string> get(std::string_view key) const {

    }

    [[nodiscard]] bool remove(std::string_view key) {

    }

    [[nodiscard]] bool contains(std::string_view key) const {

    }

    [[nodiscard]] std::size_t size() const noexcept {

    }

    [[nodiscard]] bool empty() const noexcept {

    }

    void clear() noexcept {

    }
private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
};

KVStore::KVStore(): impl_(std::make_unique<Impl>()) {}

KVStore::~KVStore() = default;

KVStore::KVStore(KVStore&&) noexcept = default;

KVStore& KVStore::operator=(KVStore&&) noexcept = default;

void KVStore::put(std::string_view key, std::string_view value) {

}
std::optional<std::string> KVStore::get(std::string_view key) const {

}

bool KVStore::remove(std::string_view key) {

}

bool KVStore::contains(std::string_view key) const {

}

std::size_t KVStore::size() const noexcept {

}

bool KVStore::empty() const noexcept {

}

void KVStore::clear() noexcept {
    
}

}