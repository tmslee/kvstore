#include "kvstore/core/snapshot.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <unordered_map>

#include "kvstore/core/store.hpp"
#include "kvstore/util/types.hpp"

namespace kvstore::core::test {

using util::ExpirationTime;

class SnapshotTest : public ::testing::Test {
   protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "snapshot_test";
        std::filesystem::create_directories(test_dir_);
        snapshot_path_ = test_dir_ / "test.snap";
        wal_path_ = test_dir_ / "test.wal";
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path snapshot_path_;
    std::filesystem::path wal_path_;
};

TEST_F(SnapshotTest, SaveAndLoad) {
    {
        Snapshot snap(snapshot_path_);
        std::unordered_map<std::string, std::string> data = {
            {"key1", "value1"},
            {"key2", "value2"},
            {"key3", "value3"},
        };

        snap.save([&data](EntryEmitter emit) {
            for (const auto& [k, v] : data) {
                emit(k, v, std::nullopt);
            }
        });

        EXPECT_TRUE(snap.exists());
        EXPECT_EQ(snap.entry_count(), 3);
    }

    {
        Snapshot snap(snapshot_path_);
        std::unordered_map<std::string, std::string> loaded;

        snap.load([&loaded](std::string_view key, std::string_view value,
                            [[maybe_unused]] ExpirationTime exp) {
            loaded[std::string(key)] = std::string(value);
        });

        EXPECT_EQ(loaded.size(), 3);
        EXPECT_EQ(loaded["key1"], "value1");
        EXPECT_EQ(loaded["key2"], "value2");
        EXPECT_EQ(loaded["key3"], "value3");
    }
}

TEST_F(SnapshotTest, SaveAndLoadWithTTL) {
    {
        Snapshot snap(snapshot_path_);

        snap.save([](EntryEmitter emit) {
            emit("key1", "value1", std::nullopt);
            emit("key2", "value2", 123456789);
            emit("key3", "value3", 987654321);
        });
    }

    {
        Snapshot snap(snapshot_path_);
        std::vector<std::tuple<std::string, std::string, ExpirationTime>> loaded;

        snap.load([&loaded](std::string_view key, std::string_view value, ExpirationTime exp) {
            loaded.emplace_back(std::string(key), std::string(value), exp);
        });

        ASSERT_EQ(loaded.size(), 3);

        // Order may vary, so find each entry
        bool found_key1 = false, found_key2 = false, found_key3 = false;
        for (const auto& [k, v, exp] : loaded) {
            if (k == "key1") {
                EXPECT_FALSE(exp.has_value());
                found_key1 = true;
            } else if (k == "key2") {
                ASSERT_TRUE(exp.has_value());
                EXPECT_EQ(exp.value(), 123456789);
                found_key2 = true;
            } else if (k == "key3") {
                ASSERT_TRUE(exp.has_value());
                EXPECT_EQ(exp.value(), 987654321);
                found_key3 = true;
            }
        }
        EXPECT_TRUE(found_key1 && found_key2 && found_key3);
    }
}

TEST_F(SnapshotTest, LoadNonexistent) {
    Snapshot snap(snapshot_path_);
    EXPECT_FALSE(snap.exists());

    int count = 0;
    snap.load([&count](std::string_view, std::string_view, ExpirationTime) { ++count; });
    EXPECT_EQ(count, 0);
}

TEST_F(SnapshotTest, StoreWithSnapshot) {
    {
        StoreOptions opts;
        opts.persistence_path = wal_path_;
        opts.snapshot_path = snapshot_path_;
        opts.snapshot_threshold = 100000;
        Store store(opts);

        store.put("key1", "value1");
        store.put("key2", "value2");
        store.snapshot();
    }
    {
        StoreOptions opts;
        opts.persistence_path = wal_path_;
        opts.snapshot_path = snapshot_path_;
        Store store(opts);

        auto result1 = store.get("key1");
        ASSERT_TRUE(result1.has_value());
        EXPECT_EQ(*result1, "value1");

        auto result2 = store.get("key2");
        ASSERT_TRUE(result2.has_value());
        EXPECT_EQ(*result2, "value2");
    }
}

TEST_F(SnapshotTest, SnapshotTruncatesWAL) {
    {
        StoreOptions opts;
        opts.persistence_path = wal_path_;
        opts.snapshot_path = snapshot_path_;
        opts.snapshot_threshold = 100000;
        Store store(opts);

        for (int i = 0; i < 100; ++i) {
            store.put("key" + std::to_string(i), "value" + std::to_string(i));
        }

        auto wal_size_before = std::filesystem::file_size(wal_path_);
        store.snapshot();
        auto wal_size_after = std::filesystem::file_size(wal_path_);

        EXPECT_LT(wal_size_after, wal_size_before);
    }
    {
        StoreOptions opts;
        opts.persistence_path = wal_path_;
        opts.snapshot_path = snapshot_path_;
        Store store(opts);

        EXPECT_EQ(store.size(), 100);
        for (int i = 0; i < 100; ++i) {
            auto result = store.get("key" + std::to_string(i));
            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(*result, "value" + std::to_string(i));
        }
    }
}

TEST_F(SnapshotTest, AutoSnapshot) {
    {
        StoreOptions opts;
        opts.persistence_path = wal_path_;
        opts.snapshot_path = snapshot_path_;
        opts.snapshot_threshold = 10;
        Store store(opts);

        for (int i = 0; i < 15; ++i) {
            store.put("key" + std::to_string(i), "value" + std::to_string(i));
        }

        EXPECT_TRUE(std::filesystem::exists(snapshot_path_));
    }
}

TEST_F(SnapshotTest, RecoveryWithSnapshotAndWAL) {
    {
        StoreOptions opts;
        opts.persistence_path = wal_path_;
        opts.snapshot_path = snapshot_path_;
        opts.snapshot_threshold = 100000;
        Store store(opts);

        store.put("key1", "value1");
        store.put("key2", "value2");
        store.snapshot();

        store.put("key3", "value3");
        store.put("key1", "updated1");
    }

    {
        StoreOptions opts;
        opts.persistence_path = wal_path_;
        opts.snapshot_path = snapshot_path_;
        Store store(opts);

        EXPECT_EQ(store.size(), 3);

        auto result1 = store.get("key1");
        ASSERT_TRUE(result1.has_value());
        EXPECT_EQ(*result1, "updated1");

        auto result2 = store.get("key2");
        ASSERT_TRUE(result2.has_value());
        EXPECT_EQ(*result2, "value2");

        auto result3 = store.get("key3");
        ASSERT_TRUE(result3.has_value());
        EXPECT_EQ(*result3, "value3");
    }
}

}  // namespace kvstore::core::test