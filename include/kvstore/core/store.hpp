#ifndef KVSTORE_CORE_STORE_HPP
#define KVSTORE_CORE_STORE_HPP

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "kvstore/util/clock.hpp"
#include "kvstore/util/types.hpp"
#include "kvstore/core/istore.hpp"

namespace kvstore::core {

struct StoreOptions {
    std::optional<std::filesystem::path> persistence_path = std::nullopt;
    std::optional<std::filesystem::path> snapshot_path = std::nullopt;
    std::size_t snapshot_threshold = 10000;  // snapshot after N WAL entries
    std::shared_ptr<util::Clock> clock = std::make_shared<util::SystemClock>();
};

class Store : public IStore {
   public:
    Store();
    explicit Store(const StoreOptions& options);
    ~Store() override;

    /*
        we delete copies bc this class will hold a mutex internally - cannot be copied.
        also copying a potentially large data store is expesive.
        if someone truly needs a copy, they should be explicit abt it (we can provide clone()
       method)

        standard containers (i.e. std::vector) check if your move ops are noexcept before
       reallocation if your ctor can throw, vector::push_back will copy instead of move for
       exception safety noexcept moves enable optimal container behavior
    */

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    Store(Store&&) noexcept;
    Store& operator=(Store&&) noexcept;

    /*
        put: allocates memory -> can throw
        remove, contains: theoretically could throw due to hash/compare operation
            - std::hash<std::string> and std::equal_to<std::string>
            - note: std::unordered_map::erase IS noexcept once you have iterator
        get: same lookup concerns as above + we return std::optional<std:;string> which means
       constructing a new std::string -> allocation = can throw.
    */
    void put(std::string_view key, std::string_view value) override;
    void put(std::string_view key, std::string_view, util::Duration ttl) override;

    [[nodiscard]] std::optional<std::string> get(std::string_view key) override;
    [[nodiscard]] bool remove(std::string_view key) override;
    /*
        note: we use string_view for read-only access (function parameters)
        other use cases: parsing/tokenizing, passing string literals, function returns when lifetime
       is guaranteed - reutrning a view into static data or member data in single-threaded context
        DO NOT use string_view:
            - need to store the data - string_view will dangle
            - returning from thread safe container (data can be modified/deleted)
            - source might go out of scope (caller left holding danglign view)
            - need null-termination - string_view is NOT null-terminated
    */

    [[nodiscard]] bool contains(std::string_view key) override;
    [[nodiscard]] std::size_t size() const override;
    [[nodiscard]] bool empty() const override;

    void clear() override;

    void snapshot();
    void cleanup_expired();

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace kvstore::core

#endif