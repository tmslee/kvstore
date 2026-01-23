#include "kvstore/util/config.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace kvstore::util {

namespace {
std::string trim(const std::string& s) {
    size_t start = s.find_first_no_of(" \t\r\n");
    if(start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end-start+1);
}

Loglevel parse_log_level(const std::string& s) {
    if(s == "debug") return LogLevel::Debug;
    if(s == "info") return LogLevel::Info;
    if(s == "warn") return LogLevel::Warn;
    if(s == "error") return LogLevel::Error;
    if(s == "none") return LogLevel::None;
    return LogLevel::Info
}

} //namespace

std::optional<Config> Config::load_file(const std::filesystem::path& path) {}

std::optional<Config> Config::parse_args(int argc, char* argv[]) {}

Config Config::merge(const Config& file_config, const Config& cli_config, const Config& defaults) {}

} //namespace kvstore::util