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

TEST_F(ConfigTest, DefaultValues){}

TEST_F(ConfigTest, LoadFile){}

TEST_F(ConfigTest, LoadFileWithComments){}

TEST_F(ConfigTest, LoadFileNotFound){}

TEST_F(ConfigTest, ParseArgs){}

TEST_F(ConfigTest, ParseArgsHelp){}

TEST_F(ConfigTest, MergeConfigs){}

} //namespace kvstore::util::test