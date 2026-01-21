#ifndef KVSTORE_CORE_WAL_HPP
#define KVSTORE_CORE_WAL_HPP

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "kvstore/util/types.hpp"

namespace kvstore::core {

enum class EntryType : uint8_t { Put = 1, PutWithTTL = 2, Remove = 3, Clear = 4 };

class WriteAheadLog {
   public:
    explicit WriteAheadLog(const std::filesystem::path& path);
    ~WriteAheadLog();

    // delete copies bc class holds open std::ofstream to WAL file.
    // copying would have 2 instances think they own the same file
    // - both would try to write/close -> corrupted data, double close, UB
    WriteAheadLog(const WriteAheadLog&) = delete;
    WriteAheadLog& operator=(const WriteAheadLog&) = delete;

    // keep moves as moving transfers ownership cleanly - source gives up handle, destination takes
    // it. only one instance ever owns the resource
    WriteAheadLog(WriteAheadLog&&) noexcept;
    WriteAheadLog& operator=(WriteAheadLog&&) noexcept;

    void log_put(std::string_view key, std::string_view value);
    void log_put_with_ttl(std::string_view key, std::string_view value, int64_t expires_at_ms);
    void log_remove(std::string_view key);
    void log_clear();

    // callback: any callable thing that takes these 3 parameters & returns void
    // note: std::function has overhead - allocates if callable is large & uses indirection
    //   - for hot paths, a template paramter is faster:
    /*
            template<typename F>
            void replay(F&& callback);
    */
    // for recovery (called once at startup) std::function is fine and keeps interface simple
    void replay(
        std::function<void(EntryType, std::string_view, std::string_view, util::ExpirationTime)>
            callback);

    void sync();
    void truncate();

    [[nodiscard]] std::filesystem::path path() const;
    [[nodiscard]] std::size_t size() const;

   private:
    void write_header();
    bool validate_header(std::ifstream& in);
    void write_entry(EntryType type, std::string_view key, std::string_view value);
    void write_entry_with_ttl(EntryType type, std::string_view key, std::string_view value,
                              int64_t expires_at_ms);

    [[nodiscard]] bool read_entry(std::ifstream& in, EntryType& type, std::string& key,
                                  std::string& value, util::ExpirationTime& expires_at);

    static constexpr uint32_t kMagic = 0x4B56574C;  // "KVWL"
    static constexpr uint32_t kVersion = 1;

    std::filesystem::path path_;
    std::ofstream out_;
    mutable std::mutex mutex_;
};

}  // namespace kvstore::core

#endif