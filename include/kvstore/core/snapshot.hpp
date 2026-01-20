#ifndef KVSTORE_CORE_SNAPSHOT_HPP
#define KVSTORE_CORE_SNAPSHOT_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view

namespace kvstore::core {

class Snapshot {
public:
    explicit Snapshot(const std::filesystem::path& path);

    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;

    Snapshot(Snapshot&&) noexcept = default;
    Snapshot& operator=(Snapshot&&) noexcept = default;

    void save(const std::function<void(std::function<void(std::string_view, std::string_view)>)> iterate);
    void load(std::function<void(std::string_view, std::string_view)> callback);

    [[nodiscard]] bool exists() const;
    [[nodiscard]] std::filesystem::path path() const;
    [[nodiscard]] std::size_t entry_count() const;

private:
    std::filesystem::path path_;
    std::size_t entry_count_ = 0;
};

}//namespace kvstore::core

#endif