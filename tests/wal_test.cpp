#include "kvstore/wal.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <vector>

namespace kvstore::test {
class WALTest : public ::testing::Test {
protected:
    void Setup() override {

    }
    void TearDown() override {

    }
    std::filesystem::path test_dir_;
    std::filesystem::path wal_path_;
};

TEST_F(WALTest, LogAndReplay) {}

TEST_F(WALTest, LogClear) {}

TEST_F(WALTest, Truncate) {}

TEST_F(WALTest, EmptyReplay) {}

} //namespace kvstore::test