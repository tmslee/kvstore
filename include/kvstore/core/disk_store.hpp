#ifndef KVSTORE_CORE_DISK_STORE_HPP
#define KVSTORE_CORE_DISK_STORE_HPP

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "kvstore/core/istore.hpp"
#include "kvstore/util/clock.hpp"
#include "kvstore/util/types.hpp"

namespace kvstore::core {

struct DiskStoreOptions {
    std::filesystem::path data_dir;
    std::size_t compaction_threshold = 1000;  // compact after N tombstones
    std::shared_ptr<util::Clock> clock = std::make_shared<util::SystemClock>();
};

class DiskStore : public IStore {
   public:
    explicit DiskStore(const DiskStoreOptions& options);
    ~DiskStore();

    DiskStore(const DiskStore&) = delete;
    DiskStore& operator=(const DiskStore&) = delete;
    DiskStore(DiskStore&&) noexcept;
    DiskStore& operator=(DiskStore&&) noexcept;

    void put(std::string_view key, std::string_view value) override;
    void put(std::string_view key, std::string_view value, util::Duration ttl) override;

    [[nodiscard]] std::optional<std::string> get(std::string_view key) override;
    [[nodiscard]] bool remove(std::string_view key) override;
    [[nodiscard]] bool contains(std::string_view key) override;
    [[nodiscard]] std::size_t size() const override;
    [[nodiscard]] bool empty() const override;

    void clear() override;
    void compact();

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace kvstore::core

#endif