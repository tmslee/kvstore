#include "kvstore/core/wal.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <vector>

namespace kvstore::core::test {

using util::ExpirationTime;

class WALTest : public ::testing::Test {
   protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "wal_test";
        std::filesystem::create_directories(test_dir_);
        wal_path_ = test_dir_ / "test.wal";
    }
    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
    std::filesystem::path test_dir_;
    std::filesystem::path wal_path_;
};

TEST_F(WALTest, LogAndReplay) {
    {
        WriteAheadLog wal(wal_path_);
        wal.log_put("key1", "value1");
        wal.log_put("key2", "value2");
        wal.log_remove("key1");
    }
    std::vector<std::tuple<EntryType, std::string, std::string, ExpirationTime>> entries;
    {
        WriteAheadLog wal(wal_path_);
        wal.replay([&entries](EntryType type, std::string_view key, std::string_view value,
                              ExpirationTime exp) {
            entries.emplace_back(type, std::string(key), std::string(value), exp);
        });
    }
    ASSERT_EQ(entries.size(), 3);
    EXPECT_EQ(std::get<0>(entries[0]), EntryType::Put);
    EXPECT_EQ(std::get<1>(entries[0]), "key1");
    EXPECT_EQ(std::get<2>(entries[0]), "value1");

    EXPECT_EQ(std::get<0>(entries[1]), EntryType::Put);
    EXPECT_EQ(std::get<1>(entries[1]), "key2");
    EXPECT_EQ(std::get<2>(entries[1]), "value2");

    EXPECT_EQ(std::get<0>(entries[2]), EntryType::Remove);
    EXPECT_EQ(std::get<1>(entries[2]), "key1");
}

TEST_F(WALTest, LogAndReplayWithTTL) {
    {
        WriteAheadLog wal(wal_path_);
        wal.log_put("key1", "value1");
        wal.log_put_with_ttl("key2", "value2", 123456789);
    }

    std::vector<std::tuple<EntryType, std::string, std::string, ExpirationTime>> entries;

    {
        WriteAheadLog wal(wal_path_);
        wal.replay([&entries](EntryType type, std::string_view key, std::string_view value,
                              ExpirationTime exp) {
            entries.emplace_back(type, std::string(key), std::string(value), exp);
        });
    }

    ASSERT_EQ(entries.size(), 2);

    EXPECT_EQ(std::get<0>(entries[0]), EntryType::Put);
    EXPECT_FALSE(std::get<3>(entries[0]).has_value());

    EXPECT_EQ(std::get<0>(entries[1]), EntryType::PutWithTTL);
    EXPECT_EQ(std::get<1>(entries[1]), "key2");
    EXPECT_EQ(std::get<2>(entries[1]), "value2");
    ASSERT_TRUE(std::get<3>(entries[1]).has_value());
    EXPECT_EQ(std::get<3>(entries[1]).value(), 123456789);
}

TEST_F(WALTest, LogClear) {
    {
        WriteAheadLog wal(wal_path_);
        wal.log_put("key1", "value1");
        wal.log_clear();
        wal.log_put("key2", "value2");
    }

    std::vector<std::tuple<EntryType, std::string, std::string, ExpirationTime>> entries;

    {
        WriteAheadLog wal(wal_path_);
        wal.replay([&entries](EntryType type, std::string_view key, std::string_view value,
                              ExpirationTime exp) {
            entries.emplace_back(type, std::string(key), std::string(value), exp);
        });
    }

    ASSERT_EQ(entries.size(), 3);
    EXPECT_EQ(std::get<0>(entries[1]), EntryType::Clear);
}

TEST_F(WALTest, Truncate) {
    {
        WriteAheadLog wal(wal_path_);
        wal.log_put("key1", "value1");
        wal.log_put("key2", "value2");
        wal.truncate();
        wal.log_put("key3", "value3");
    }

    std::vector<std::tuple<EntryType, std::string, std::string, ExpirationTime>> entries;

    {
        WriteAheadLog wal(wal_path_);
        wal.replay([&entries](EntryType type, std::string_view key, std::string_view value,
                              ExpirationTime exp) {
            entries.emplace_back(type, std::string(key), std::string(value), exp);
        });
    }

    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(std::get<1>(entries[0]), "key3");
}

TEST_F(WALTest, EmptyReplay) {
    WriteAheadLog wal(wal_path_);

    int count = 0;
    wal.replay(
        [&count](EntryType, std::string_view, std::string_view, ExpirationTime) { ++count; });

    EXPECT_EQ(count, 0);
}

}  // namespace kvstore::core::test