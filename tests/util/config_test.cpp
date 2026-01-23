#include "kvstore/util/config.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace kvstore::util::test {

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "config_test";
        std::filesystem::create_directories(test_dir_);
    }
    
    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
};

TEST_F(ConfigTest, DefaultValues){
    Config config;
    EXPECT_EQ(config.host, "127.0.0.1");
    EXPECT_EQ(config.port, 6379);
    EXPECT_EQ(config.max_connections, 1000);
    EXPECT_EQ(config.data_dir, "./data");
    EXPECT_EQ(config.log_level, LogLevel::Info);
    EXPECT_FALSE(config.use_disk_store);
}

TEST_F(ConfigTest, LoadFile){
    auto path = test_dir_ / "test.conf";
    {
        std::ofstream f(path);
        f << "host = \"0.0.0.0\"\n";
        f << "port = 8080\n";
        f << "log_level = debug\n";
        f << "use_disk_store = true\n";
    }

    auto config = Config::load_file(path);
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->host, "0.0.0.0");
    EXPECT_EQ(config->port, 8080);
    EXPECT_EQ(config->log_level, LogLevel::Debug);
    EXPECT_TRUE(config->use_disk_store);
}

TEST_F(ConfigTest, LoadFileWithComments){
    auto path = test_dir_ / "test.conf";
    {
        std::ofstream f(path);
        f << "# This is a comment\n";
        f << "port = 9000\n";
        f << "\n";
        f << "# Another comment\n";
        f << "host = \"localhost\"\n";
    }

    auto config = Config::load_file(path);
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->port, 9000);
    EXPECT_EQ(config->host, "localhost");
}

TEST_F(ConfigTest, LoadFileNotFound){
    auto config = Config::load_file("/nonexistent/path/config.conf");
    EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, ParseArgs){
    const char* argv[] = {"program", "-p", "8080", "-H", "0.0.0.0", "-l", "debug"};
    int argc = 7;

    auto config = Config::parse_args(argc, const_cast<char**>(argv));
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->port, 8080);
    EXPECT_EQ(config->host, "0.0.0.0");
    EXPECT_EQ(config->log_level, LogLevel::Debug);
}

TEST_F(ConfigTest, ParseArgsHelp){
    const char* argv[] = {"program", "--help"};
    int argc = 2;

    auto config = Config::parse_args(argc, const_cast<char**>(argv));
    EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, MergeConfigs){
    Config defaults;
    Config file_config = defaults;
    Config cli_config = defaults;

    file_config.port = 8080;
    file_config.host = "0.0.0.0";

    cli_config.port = 9000;  // CLI overrides file

    auto result = Config::merge(file_config, cli_config, defaults);

    EXPECT_EQ(result.port, 9000);       // CLI wins
    EXPECT_EQ(result.host, "0.0.0.0");  // File wins (CLI was default)
}

} //namespace kvstore::util::test