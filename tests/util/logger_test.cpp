#include "kvstore/util/logger.hpp"

#include <gtest/gtest.h>

namespace kvstore::util::test {

TEST(LoggerTest, DefautlLevelIsInfo) {
    EXPECT_EQ(Logger::instance().level(), LogLevel::Info);
}

TEST(LoggerTest, SetLevel) {
    Logger::instance().set_level(LogLevel::Debug);
    EXPECT_EQ(Logger::instance().level(), LogLevel::Debug);

    Logger::instance().set_level(LogLevel::Error);
    EXPECT_EQ(Logger::instance().level(), LogLevel::Error);

    // reset to default
    Logger::instance().set_level(LogLevel::Info);
}

TEST(LoggerTest, LogMethods) {
    // these just verify no crash- output goes to stdout/stderr
    Logger::instance().set_level(LogLevel::Debug);

    Logger::instance().debug("debug message");
    Logger::instance().info("info message");
    Logger::instance().warn("warn message");
    Logger::instance().error("error message");

    LOG_DEBUG("macro debug");
    LOG_INFO("macro info");
    LOG_WARN("macro warn");
    LOG_ERROR("macro error");

    logger::instance().set_level(LogLevel::Info);
}

TEST(LoggerTest, LevelFiltering) {
    Logger::instance().set_level(LogLevel::Warn);

    // these should be filtered out (no crash, just no output)
    Logger::instance().debug("filtered debug");
    Logger::instance().info("filtered info");

    // these should appear
    Logger::instance().warn("visible warn");
    Logger::instance().error("visible error");

    Logger::instance().set_level(LogLevel : Info);
}

}  // namespace kvstore::util::test