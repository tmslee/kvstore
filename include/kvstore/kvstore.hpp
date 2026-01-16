#ifndef KVSTORE_KVSTORE_HPP
#define KVSTORE_KVSTORE_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kvstore {

struct Options {
    std::optional<std::filesystem::path> persistence_path = std::nullopt;
};

class KVStore {
   public:
    KVStore();
    ~KVStore();
    /*
        we delete copies bc this class will hold a mutex internally - cannot be copied.
        also copying a potentially large data store is expesive.
        if someone truly needs a copy, they should be explicit abt it (we can provide clone()
       method)
    */
    KVStore(const KVStore&) = delete;
    KVStore& operator=(const KVStore&) = delete;
    /*
        standard containers (i.e. std::vector) check if your move ops are noexcept before
       reallocation if your ctor can throw, vector::push_back will copy instead of move for
       exception safety noexcept moves enable optimal container behavior
    */
    KVStore(KVStore&&) noexcept;
    KVStore& operator=(KVStore&&) noexcept;

    /*
        put: allocates memory -> can throw
        remove, contains: theoretically could throw due to hash/compare operation
            - std::hash<std::string> and std::equal_to<std::string>
            - note: std::unordered_map::erase IS noexcept once you have iterator
        get: same lookup concerns as above + we return std::optional<std:;string> which means
       constructing a new std::string -> allocation = can throw.
    */
    void put(std::string_view key, std::string_view value);
    [[nodiscard]] std::optional<std::string> get(std::string_view key) const;
    [[nodiscard]] bool remove(std::string_view key);

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

    [[nodiscard]] bool contains(std::string_view key) const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    void clear() noexcept;

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace kvstore

#endif