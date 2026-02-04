#include "kvstore/core/wal.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
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

// =============================================================================
// WAL Corruption Recovery Tests
// =============================================================================

TEST_F(WALTest, BadMagicNumber) {
    // Create a file with invalid magic number
    {
        std::ofstream f(wal_path_, std::ios::binary);
        uint32_t bad_magic = 0xDEADBEEF;
        uint32_t version = 1;
        f.write(reinterpret_cast<char*>(&bad_magic), sizeof(bad_magic));
        f.write(reinterpret_cast<char*>(&version), sizeof(version));
    }

    WriteAheadLog wal(wal_path_);
    EXPECT_THROW(wal.replay([](EntryType, std::string_view, std::string_view, ExpirationTime) {}),
                 std::runtime_error);
}

TEST_F(WALTest, BadVersion) {
    // Create a file with valid magic but wrong version
    {
        std::ofstream f(wal_path_, std::ios::binary);
        uint32_t magic = 0x4B56574C;  // "KVWL"
        uint32_t bad_version = 999;
        f.write(reinterpret_cast<char*>(&magic), sizeof(magic));
        f.write(reinterpret_cast<char*>(&bad_version), sizeof(bad_version));
    }

    WriteAheadLog wal(wal_path_);
    EXPECT_THROW(wal.replay([](EntryType, std::string_view, std::string_view, ExpirationTime) {}),
                 std::runtime_error);
}

TEST_F(WALTest, TruncatedHeader) {
    // Create a file with truncated header (only magic, no version)
    {
        std::ofstream f(wal_path_, std::ios::binary);
        uint32_t magic = 0x4B56574C;
        f.write(reinterpret_cast<char*>(&magic), sizeof(magic));
        // no version written
    }

    WriteAheadLog wal(wal_path_);
    EXPECT_THROW(wal.replay([](EntryType, std::string_view, std::string_view, ExpirationTime) {}),
                 std::runtime_error);
}

TEST_F(WALTest, TruncatedEntryMidWrite) {
    // Write valid entries, then simulate crash by truncating mid-entry
    {
        WriteAheadLog wal(wal_path_);
        wal.log_put("key1", "value1");
        wal.log_put("key2", "value2");
    }

    // Append partial entry (type byte + partial key length) to simulate crash mid-write
    {
        std::ofstream f(wal_path_, std::ios::binary | std::ios::app);
        uint8_t type = 1;  // Put
        f.write(reinterpret_cast<char*>(&type), sizeof(type));
        uint32_t partial_len = 100;  // key length that won't have enough data
        f.write(reinterpret_cast<char*>(&partial_len), sizeof(partial_len));
        // no key data written - simulates crash
    }

    // Replay should recover the valid entries before the corruption
    std::vector<std::string> keys;
    {
        WriteAheadLog wal(wal_path_);
        wal.replay([&keys](EntryType, std::string_view key, std::string_view, ExpirationTime) {
            keys.emplace_back(key);
        });
    }

    // Should have replayed the 2 valid entries before the truncated one
    ASSERT_EQ(keys.size(), 2);
    EXPECT_EQ(keys[0], "key1");
    EXPECT_EQ(keys[1], "key2");
}

TEST_F(WALTest, PartialEntryAtEnd) {
    // Write valid entry then just a type byte (simulates crash after type write)
    {
        WriteAheadLog wal(wal_path_);
        wal.log_put("good_key", "good_value");
    }

    // Append just the type byte for next entry
    {
        std::ofstream f(wal_path_, std::ios::binary | std::ios::app);
        uint8_t type = 1;  // Put
        f.write(reinterpret_cast<char*>(&type), sizeof(type));
        // crash - no key/value data
    }

    std::vector<std::string> keys;
    {
        WriteAheadLog wal(wal_path_);
        wal.replay([&keys](EntryType, std::string_view key, std::string_view, ExpirationTime) {
            keys.emplace_back(key);
        });
    }

    ASSERT_EQ(keys.size(), 1);
    EXPECT_EQ(keys[0], "good_key");
}

TEST_F(WALTest, ZeroByteFile) {
    // Create empty file
    { std::ofstream f(wal_path_, std::ios::binary); }
    EXPECT_EQ(std::filesystem::file_size(wal_path_), 0);

    // Opening WAL on empty file should write header
    WriteAheadLog wal(wal_path_);
    EXPECT_GT(std::filesystem::file_size(wal_path_), 0);

    // Replay should work with no entries
    int count = 0;
    wal.replay(
        [&count](EntryType, std::string_view, std::string_view, ExpirationTime) { ++count; });
    EXPECT_EQ(count, 0);
}

TEST_F(WALTest, JustHeaderNoEntries) {
    // Write valid header only
    {
        std::ofstream f(wal_path_, std::ios::binary);
        uint32_t magic = 0x4B56574C;  // "KVWL"
        uint32_t version = 1;
        f.write(reinterpret_cast<char*>(&magic), sizeof(magic));
        f.write(reinterpret_cast<char*>(&version), sizeof(version));
    }

    WriteAheadLog wal(wal_path_);
    int count = 0;
    wal.replay(
        [&count](EntryType, std::string_view, std::string_view, ExpirationTime) { ++count; });
    EXPECT_EQ(count, 0);
}

TEST_F(WALTest, CorruptedKeyLength) {
    // Write valid entry, then entry with impossibly large key length
    {
        WriteAheadLog wal(wal_path_);
        wal.log_put("key1", "value1");
    }

    // Append entry with huge key length (simulates corruption)
    {
        std::ofstream f(wal_path_, std::ios::binary | std::ios::app);
        uint8_t type = 1;                // Put
        uint32_t huge_len = 0xFFFFFFFF;  // 4GB key - will fail to read
        f.write(reinterpret_cast<char*>(&type), sizeof(type));
        f.write(reinterpret_cast<char*>(&huge_len), sizeof(huge_len));
    }

    // Replay should recover valid entries before corruption
    std::vector<std::string> keys;
    {
        WriteAheadLog wal(wal_path_);
        wal.replay([&keys](EntryType, std::string_view key, std::string_view, ExpirationTime) {
            keys.emplace_back(key);
        });
    }

    ASSERT_EQ(keys.size(), 1);
    EXPECT_EQ(keys[0], "key1");
}

}  // namespace kvstore::core::test