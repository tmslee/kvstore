#ifndef KVSTORE_UTIL_CONFIG_HPP
#define KVSTORE_UTIL_CONFIG_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "kvstore/util/logger.hpp"

namespace kvstore::util {

struct Config {
    // server
    std::string host = "127.0.0.1";
    uint16_t port = 6379;
    std::size_t max_connections = 1000;
    int client_timeout_seconds = 300;

    // storage
    std::filesystem::path data_dir = "./data";
    std::size_t snapshot_threshold = 10000;
    std::size_t compaction_threshold = 1000;
    bool use_disk_store = false;

    // logging
    LogLevel log_level = LogLevel::Info;

    // Load from file (TOML-like format)
    static std::optional<Config> load_file(const std::filesystem::path& path);

    // parse CLI args, returns nullopt on --help or error
    static std::optional<Config> parse_args(int argc, char* argv[]);

    // merge: CLI overrides file
    static Config merge(const Config& file_config, const Config& cli_config,
                        const Config& defaults);
};

}  // namespace kvstore::util

#endif