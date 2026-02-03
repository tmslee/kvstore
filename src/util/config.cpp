#include "kvstore/util/config.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace kvstore::util {

namespace {
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

LogLevel parse_log_level(const std::string& s) {
    if (s == "debug")
        return LogLevel::Debug;
    if (s == "info")
        return LogLevel::Info;
    if (s == "warn")
        return LogLevel::Warn;
    if (s == "error")
        return LogLevel::Error;
    if (s == "none")
        return LogLevel::None;
    return LogLevel::Info;
}

// note: we use cerr here becuase we initialize logger AFTER config parsing when server is
// initialized
int safe_stoi(const std::string& s, int default_val, const std::string& field = "") {
    try {
        return std::stoi(s);
    } catch (const std::exception&) {
        if (!field.empty()) {
            std::cerr << "Warning: Invalid value for " << field << ": '" << s
                      << "' using default\n";
        }
        return default_val;
    }
}

uint64_t safe_stoull(const std::string& s, uint64_t default_val, const std::string& field = "") {
    try {
        return std::stoull(s);
    } catch (const std::exception&) {
        if (!field.empty()) {
            std::cerr << "Warning: Invalid value for " << field << ": '" << s
                      << "', using default\n";
        }
        return default_val;
    }
}

}  // namespace

std::optional<Config> Config::load_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    Config config;
    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);

        if (line.empty() || line[0] == '#') {
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        // remove quotes if present
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        if (key == "host") {
            config.host = value;
        } else if (key == "port") {
            config.port = static_cast<uint16_t>(safe_stoi(value, 6379, "port"));
        } else if (key == "max_connections") {
            config.max_connections = safe_stoull(value, 1000, "max_connections");
        } else if (key == "client_timeout_seconds") {
            config.client_timeout_seconds = safe_stoi(value, 300, "client_timeout_seconds");
        } else if (key == "data_dir") {
            config.data_dir = value;
        } else if (key == "snapshot_threshold") {
            config.snapshot_threshold = safe_stoull(value, 10000, "snapshot_threshold");
        } else if (key == "compaction_threshold") {
            config.compaction_threshold = safe_stoull(value, 1000, "compaction_threshold");
        } else if (key == "use_disk_store") {
            config.use_disk_store = (value == "true" || value == "1");
        } else if (key == "log_level") {
            config.log_level = parse_log_level(value);
        } else if (key == "max_message_size") {
            config.max_message_size = safe_stoull(value, 67108864, "max_message_size");
        } else if (key == "max_key_size") {
            config.max_key_size = safe_stoull(value, 1024, "max_key_size");
        } else if (key == "max_value_size") {
            config.max_value_size = safe_stoull(value, 67108864, "max_value_size");
        }
    }

    return config;
}

std::optional<Config> Config::parse_args(int argc, char* argv[]) {
    Config config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout
                << "Usage: " << argv[0] << " [options]\n"
                << "Options:\n"
                << "  -c, --config FILE          Config file path\n"
                << "  -H, --host HOST            Host to bind (default: 127.0.0.1)\n"
                << "  -p, --port PORT            Port to listen on (default: 6379)\n"
                << "  -d, --data-dir DIR         Data directory (default: ./data)\n"
                << "  -l, --log-level LEVEL      Log level: debug, info, warn, error, none\n"
                << "  --max-connections N        Max client connections (default: 1000)\n"
                << "  --client-timeout SEC       Client timeout seconds (default: 300)\n"
                << "  --snapshot-threshold N     WAL entries before snapshot (default: 10000)\n"
                << "  --compaction-threshold N   Tombstones before compaction (default: 1000)\n"
                << "  --disk-store               Use disk-based storage\n"
                << "  --max-message-size BYTES   Max message size in bytes (default: 67108864)\n"
                << "  --max-key-size BYTES       Max key size in bytes (default: 1024)\n"
                << "  --max-value-size BYTES     Max value size in bytes (default: 67108864)\n"
                << "  -h, --help                 Show this help\n";
            return std::nullopt;
        }
        if ((arg == "-H" || arg == "--host") && i + 1 < argc) {
            config.host = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            config.port = static_cast<uint16_t>(safe_stoi(argv[++i], 6379, "port"));
        } else if ((arg == "-d" || arg == "--data-dir") && i + 1 < argc) {
            config.data_dir = argv[++i];
        } else if ((arg == "-l" || arg == "--log-level") && i + 1 < argc) {
            config.log_level = parse_log_level(argv[++i]);
        } else if (arg == "--max-connections" && i + 1 < argc) {
            config.max_connections = safe_stoull(argv[++i], 1000, "max_connections");
        } else if (arg == "--client-timeout" && i + 1 < argc) {
            config.client_timeout_seconds = safe_stoi(argv[++i], 300, "client_timeout_seconds");
        } else if (arg == "--snapshot-threshold" && i + 1 < argc) {
            config.snapshot_threshold = safe_stoull(argv[++i], 10000, "snapshot_threshold");
        } else if (arg == "--compaction-threshold" && i + 1 < argc) {
            config.compaction_threshold = safe_stoull(argv[++i], 1000, "compaction_threshold");
        } else if (arg == "--disk-store") {
            config.use_disk_store = true;
        } else if (arg == "--max-message-size" && i + 1 < argc) {
            config.max_message_size = safe_stoull(argv[++i], 67108864, "max_message_size");
        } else if (arg == "--max-key-size" && i + 1 < argc) {
            config.max_key_size = safe_stoull(argv[++i], 1024, "max_key_size");
        } else if (arg == "--max-value-size" && i + 1 < argc) {
            config.max_value_size = safe_stoull(argv[++i], 67108864, "max_value_size");
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            // Config file handled separately in main
            ++i;
        }
    }

    return config;
}

Config Config::merge(const Config& file_config, const Config& cli_config, const Config& defaults) {
    Config result = defaults;
    // File overrides defaults
    if (file_config.host != defaults.host)
        result.host = file_config.host;
    if (file_config.port != defaults.port)
        result.port = file_config.port;
    if (file_config.max_connections != defaults.max_connections)
        result.max_connections = file_config.max_connections;
    if (file_config.client_timeout_seconds != defaults.client_timeout_seconds)
        result.client_timeout_seconds = file_config.client_timeout_seconds;
    if (file_config.data_dir != defaults.data_dir)
        result.data_dir = file_config.data_dir;
    if (file_config.snapshot_threshold != defaults.snapshot_threshold)
        result.snapshot_threshold = file_config.snapshot_threshold;
    if (file_config.compaction_threshold != defaults.compaction_threshold)
        result.compaction_threshold = file_config.compaction_threshold;
    if (file_config.use_disk_store != defaults.use_disk_store)
        result.use_disk_store = file_config.use_disk_store;
    if (file_config.log_level != defaults.log_level)
        result.log_level = file_config.log_level;
    if (file_config.max_message_size != defaults.max_message_size)
        result.max_message_size = file_config.max_message_size;
    if (file_config.max_key_size != defaults.max_key_size)
        result.max_key_size = file_config.max_key_size;
    if (file_config.max_value_size != defaults.max_value_size)
        result.max_value_size = file_config.max_value_size;

    // CLI overrides file
    if (cli_config.host != defaults.host)
        result.host = cli_config.host;
    if (cli_config.port != defaults.port)
        result.port = cli_config.port;
    if (cli_config.max_connections != defaults.max_connections)
        result.max_connections = cli_config.max_connections;
    if (cli_config.client_timeout_seconds != defaults.client_timeout_seconds)
        result.client_timeout_seconds = cli_config.client_timeout_seconds;
    if (cli_config.data_dir != defaults.data_dir)
        result.data_dir = cli_config.data_dir;
    if (cli_config.snapshot_threshold != defaults.snapshot_threshold)
        result.snapshot_threshold = cli_config.snapshot_threshold;
    if (cli_config.compaction_threshold != defaults.compaction_threshold)
        result.compaction_threshold = cli_config.compaction_threshold;
    if (cli_config.use_disk_store != defaults.use_disk_store)
        result.use_disk_store = cli_config.use_disk_store;
    if (cli_config.log_level != defaults.log_level)
        result.log_level = cli_config.log_level;
    if (cli_config.max_message_size != defaults.max_message_size)
        result.max_message_size = cli_config.max_message_size;
    if (cli_config.max_key_size != defaults.max_key_size)
        result.max_key_size = cli_config.max_key_size;
    if (cli_config.max_value_size != defaults.max_value_size)
        result.max_value_size = cli_config.max_value_size;

    return result;
}

}  // namespace kvstore::util