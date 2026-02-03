#include "kvstore/util/logger.hpp"

#include <gtest/gtest.h>

namespace kvstore::util::test {

// RAII guard to save and restore log level
class LogLevelGuard {
   public:
    LogLevelGuard() : saved_level_(Logger::instance().level()) {}
    ~LogLevelGuard() {
        Logger::instance().set_level(saved_level_);
    }

    LogLevelGuard(const LogLevelGuard&) = delete;
    LogLevelGuard& operator=(const LogLevelGuard&) = delete;

   private:
    LogLevel saved_level_;
};

class LoggerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // reset to known state before each test
        Logger::instance().set_level(LogLevel::Info);
    }

    void TearDown() override {
        // reset after each test (redundant with guard, but ensures clean state)
        Logger::instance().set_level(LogLevel::Info);
    }
};

TEST_F(LoggerTest, DefaultLevelIsInfo) {
    EXPECT_EQ(Logger::instance().level(), LogLevel::Info);
}

TEST_F(LoggerTest, SetLevel) {
    LogLevelGuard guard;

    Logger::instance().set_level(LogLevel::Debug);
    EXPECT_EQ(Logger::instance().level(), LogLevel::Debug);

    Logger::instance().set_level(LogLevel::Error);
    EXPECT_EQ(Logger::instance().level(), LogLevel::Error);
}

TEST_F(LoggerTest, LogMethods) {
    LogLevelGuard guard;

    // these just verify no crash - output goes to stdout/stderr
    Logger::instance().set_level(LogLevel::Debug);

    Logger::instance().debug("debug message");
    Logger::instance().info("info message");
    Logger::instance().warn("warn message");
    Logger::instance().error("error message");

    LOG_DEBUG("macro debug");
    LOG_INFO("macro info");
    LOG_WARN("macro warn");
    LOG_ERROR("macro error");
}

TEST_F(LoggerTest, LevelFiltering) {
    LogLevelGuard guard;

    Logger::instance().set_level(LogLevel::Warn);

    // these should be filtered out (no crash, just no output)
    Logger::instance().debug("filtered debug");
    Logger::instance().info("filtered info");

    // these should appear
    Logger::instance().warn("visible warn");
    Logger::instance().error("visible error");
}

}  // namespace kvstore::util::test