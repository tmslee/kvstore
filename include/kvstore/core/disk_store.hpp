#ifndef KVSTORE_CORE_DISK_STORE_HPP
#define KVSTORE_CORE_DISK_STORE_HPP

#include "kvstore/util/clock.hpp"
#include "kvstore/util/types.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kvstore::core {

struct DiskStoreOptions {
    std::filesystem::path data_dir;
    std::size_t compaction_threshold = 1000; //compact after N tombstones
    std::shared_ptr<util::Clock> clock = std::make_shared<util::SystemClock>();
};

class DiskStore {
public:
    explicit DiskStore(const DiskStoreOptions& options);
    ~DiskStore();

    DiskStore(const DiskStore&) = delete;
    DiskStore& operator=(const DiskStore&) = delete;
    DiskStore(DiskStore&&) noexcept;
    DiskStore& operator=(DiskStore&&) noexcept;

    void put(std::string_view key, std::string_view value);
    void put(std::string_view key, std::string_view value, util::Duration ttl);

    [[nodiscard]] std::optional<std::string> get(std::string_view key);
    [[nodiscard]] bool remove(std::string_view key);
    [[nodiscard]] bool contains(std::string_view key);
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    void clear();
    void compact();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} //namespace kvstore::core

#endif