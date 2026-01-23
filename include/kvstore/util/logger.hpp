#ifndef KVSTORE_UTIL_LOGGER_HPP
#define KVSTORE_UTIL_LOGGER_HPP

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace kvstore::util {

enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
    None = 4
};

class Logger {
public:
    static Logger& instance();

    void set_level(LogLevel level);
    [[nodiscard]] LogLevel level() const;

    void debug(std::string_view message);
    void info(std::string_view message);
    void warn(std::string_view message);
    void error(std::string_view message);

    void log(LogLevel level, std::string_view message);

private:
    Logger() = default;

    [[nodiscard]] std::string timestamp() const;
    [[nodiscard]] std::string_view level_string(LogLevel level) const;

    LogLevel level_ = LogLevel::Info;
    std::mutex mutex_;
};

//convenience macros
#define LOG_DEBUG(msg) kvstore::util::Logger::instance().debug(msg);
#define LOG_INFO(msg) kvstore::util::Logger::instance().info(msg);
#define LOG_WARN(msg) kvstore::util::Logger::instance().warn(msg);
#define LOG_ERROR(msg) kvstore::util::Logger::instance().error(msg);

} //namespace kvstore::util

#endif