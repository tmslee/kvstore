#ifndef KVSTORE_CORE_SNAPSHOT_HPP
#define KVSTORE_CORE_SNAPSHOT_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace kvstore::core {

using EntryEmitter = std::function<void(std::string_view, std::string_view)>;
using EntryIterator = std::function<void(EntryEmitter)>;

class Snapshot {
   public:
    explicit Snapshot(const std::filesystem::path& path);

    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;

    Snapshot(Snapshot&&) noexcept = default;
    Snapshot& operator=(Snapshot&&) noexcept = default;

    /*
        we have a layer of abstraction here for the save method - it takes EntryIterator as its
       argument EntryIterator itself is a function that takes another function EntryEmitter that
       takes 2 std::string_view args (key and value).
        - this way we let the caller control WHICH data entries while we decide HOW the data is
       written here.
        - benefits:
            - decoupling - snapshot doesnt know about Store's internals
            - lock safety - Store holds lock while iterating, snapshot doesnt need to know
            - flexibility - could snapshot from ANY data source, not just unordered_map
        - negatives:
            - performance - std::function has overhead (type erasure, possible heap allocation)
                - performance concern is minor: snapshoting is infrequent and IO-bound anyway.
                - std::function overhead is noise compared to disk writes
            - debugging - stack traces through lambdas are ugly
    */
    void save(const EntryIterator& iterate);
    void load(std::function<void(std::string_view, std::string_view)> callback);

    [[nodiscard]] bool exists() const;
    [[nodiscard]] std::filesystem::path path() const;
    [[nodiscard]] std::size_t entry_count() const;

   private:
    std::filesystem::path path_;
    std::size_t entry_count_ = 0;
};

}  // namespace kvstore::core

#endif