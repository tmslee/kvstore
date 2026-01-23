#ifndef KVSTORE_CORE_ISTORE_HPP
#define KVSTORE_CORE_ISTORE_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "kvstore/util/types.hpp"

namespace kvstore::core {

class IStore {
   public:
    virtual ~IStore() = default;

    virtual void put(std::string_view key, std::string_view value) = 0;
    virtual void put(std::string_view key, std::string_view value, util::Duration ttl) = 0;

    [[nodiscard]] virtual std::optional<std::string> get(std::string_view key) = 0;
    [[nodiscard]] virtual bool remove(std::string_view key) = 0;
    [[nodiscard]] virtual bool contains(std::string_view key) = 0;
    [[nodiscard]] virtual std::size_t size() const = 0;
    [[nodiscard]] virtual bool empty() const = 0;

    virtual void clear() = 0;
    virtual void flush() = 0;
};

}  // namespace kvstore::core

#endif