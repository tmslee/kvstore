#include "kvstore/util/logger.hpp"

namespace kvstore::util {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::set_level(LogLevel level) {
    std::lock_guard lock(mutex_);
    level_ = level;
}

LogLevel Logger::level() const {
    return level_;
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
    if(level < level_) {
        return;
    }

    std::lock_guard lock(mutex_);

    std::ostream& out = (level >= LogLevel::Warn) ? std::cerr : std::cout;
    out << timestamp() << " [" << level_string(level) << "] " << message << std::endl;
}

std::string Logger::timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string_view Logger::level_string(LogLevel level) const {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        default:              return "?????";
    }
}

} //namespace kvstore::util