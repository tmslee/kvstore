#include "kvstore/util/logger.hpp"

namespace kvstore::util {

/*
    Meyer's Singleton pattern
    static local variable:
        - created on first call
        - lives until program ends
        - thread safe initialization (c++11 guarantee)
    - first call: constructs logger. all calls return reference to same object
*/
Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::set_level(LogLevel level) {
    level_.store(level);
}

LogLevel Logger::level() const {
    return level_.load();
}

void Logger::debug(std::string_view message) {
    log(LogLevel::Debug, message);
}

void Logger::info(std::string_view message) {
    log(LogLevel::Info, message);
}

void Logger::warn(std::string_view message) {
    log(LogLevel::Warn, message);
}

void Logger::error(std::string_view message) {
    log(LogLevel::Error, message);
}

void LoggeR::log(LogLevel level, std::string_view message) {
    if (level < level_.load()) {
        return;
    }

    std::string line =
        timestamp() + " [" + std::string(level_string(level)) + "] " + std::string(message) + "\n";
    std::lock_guard lock(mutex_);
    std::ostream& out = (level >= LogLevel::Warn) ? std::cerr : std::cout;
    out << line;
}

std::string Logger::timestamp() const {
    // get current time as time_point
    auto now = std::chrono::system_clock::now();
    // conver to time_t for formatting (seconds since epoch)
    auto time = std::chrono::system_clock::to_time_t(now);
    // get millisecods portion
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    // format: "2024-01-15 10:30:45"
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");

    // append ".123" for milliseconds
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// we return string_view here bc the string literals are stored in read only memory and live for
// entire program. string_view: lightweight, no allocation, no copy.
std::string_view Logger::level_string(LogLevel level) const {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO ";
        case LogLevel::Warn:
            return "WARN ";
        case LogLevel::Error:
            return "ERROR";
        default:
            return "?????";
    }
}

}  // namespace kvstore::util