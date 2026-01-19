#include "kvstore/kvstore.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace kvstore::test {

class KVStoreTest : public ::testing::Test {
   protected:
    KVStore store;
};

TEST_F(KVStoreTest, InitiallyEmpty) {
    EXPECT_TRUE(store.empty());
    EXPECT_EQ(store.size(), 0);
}

TEST_F(KVStoreTest, PutAndGet) {
    store.put("key1", "value1");
    auto result = store.get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value1");
}

TEST_F(KVStoreTest, GetMissingKey) {
    auto result = store.get("nonexistent");
    EXPECT_FALSE(result.has_value());
}

TEST_F(KVStoreTest, PutOverwrites) {
    store.put("key1", "value1");
    store.put("key1", "value2");
    auto result = store.get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value2");
}

TEST_F(KVStoreTest, Contains) {
    EXPECT_FALSE(store.contains("key1"));
    store.put("key1", "value1");
    EXPECT_TRUE(store.contains("key1"));
}

TEST_F(KVStoreTest, Remove) {
    store.put("key1", "value1");
    EXPECT_TRUE(store.remove("key1"));
    EXPECT_FALSE(store.contains("key1"));
    EXPECT_FALSE(store.remove("key1"));
}

TEST_F(KVStoreTest, Clear) {
    store.put("key1", "value1");
    store.put("key2", "value2");
    store.put("key3", "value3");
    store.clear();
    EXPECT_TRUE(store.empty());
    EXPECT_EQ(store.size(), 0);
    EXPECT_FALSE(store.contains("key1"));
}

TEST_F(KVStoreTest, Size) {
    EXPECT_EQ(store.size(), 0);
    store.put("key1", "value1");
    EXPECT_EQ(store.size(), 1);
    store.put("key2", "value2");
    EXPECT_EQ(store.size(), 2);
    store.put("key1", "newvalue");
    EXPECT_EQ(store.size(), 2);
    (void)store.remove("key1");
    EXPECT_EQ(store.size(), 1);
}

TEST_F(KVStoreTest, EmptyKeyAndValue) {
    store.put("", "empty_key_value");
    store.put("empty_value", "");

    auto res1 = store.get("");
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(*res1, "empty_key_value");

    auto res2 = store.get("empty_value");
    ASSERT_TRUE(res2.has_value());
    EXPECT_EQ(*res2, "");
}

TEST_F(KVStoreTest, ConcurrentWrites) {
    constexpr int kNumThreads = 10;
    constexpr int kWritesPerThread = 1000;

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);
    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([this, t] {
            for (int i = 0; i < kWritesPerThread; ++i) {
                std::string key = "thread" + std::to_string(t) + "_key" + std::to_string(i);
                std::string val = "value" + std::to_string(i);
                store.put(key, val);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    EXPECT_EQ(store.size(), kNumThreads * kWritesPerThread);
}

TEST_F(KVStoreTest, ConcurrentReadsAndWrites) {
    // concurrency stress test - verify that store doesnt crash/deadlock/corrupt under concurrent
    // r/w pressure a broken locking logic would likely trigger UB; crashes, hangs or sanitizer
    // errors

    constexpr int kNumReaders = 5;
    constexpr int kNumWriters = 5;
    constexpr int kOpsPerThread = 1000;

    store.put("shared_key", "initial");

    std::vector<std::thread> threads;
    threads.reserve(kNumReaders + kNumWriters);

    for (int t = 0; t < kNumWriters; ++t) {
        threads.emplace_back([this, t] {
            for (int i = 0; i < kOpsPerThread; ++i) {
                store.put("shared_key", "writer" + std::to_string(t) + "_" + std::to_string(i));
            }
        });
    }

    for (int t = 0; t < kNumReaders; ++t) {
        threads.emplace_back([this] {
            for (int i = 0; i < kOpsPerThread; ++i) {
                [[maybe_unused]] auto result = store.get("shared_key");
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
    EXPECT_TRUE(store.contains("shared_key"));
}

class KVStorePersistenceTest : public ::testing::Test {
   protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "kvstore_test";
        std::filesystem::create_directories(test_dir_);
        wal_path_ = test_dir_ / "test.wal";
    }
    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
    std::filesystem::path test_dir_;
    std::filesystem::path wal_path_;
};

TEST_F(KVStorePersistenceTest, PersistsAcrossRestarts) {
    {
        kvstore::Options opts;
        opts.persistence_path = wal_path_;
        kvstore::KVStore store(opts);

        store.put("key1", "value1");
        store.put("key2", "value2");
    }
    {
        kvstore::Options opts;
        opts.persistence_path = wal_path_;
        kvstore::KVStore store(opts);

        auto result1 = store.get("key1");
        ASSERT_TRUE(result1.has_value());
        EXPECT_EQ(*result1, "value1");

        auto result2 = store.get("key2");
        ASSERT_TRUE(result2.has_value());
        EXPECT_EQ(*result2, "value2");
    }
}

TEST_F(KVStorePersistenceTest, PersistsRemove) {
    {
        kvstore::Options opts;
        opts.persistence_path = wal_path_;
        kvstore::KVStore store(opts);

        store.put("key1", "value1");
        store.put("key2", "value2");
        (void)store.remove("key1");
    }

    {
        kvstore::Options opts;
        opts.persistence_path = wal_path_;
        kvstore::KVStore store(opts);

        EXPECT_FALSE(store.contains("key1"));
        EXPECT_TRUE(store.contains("key2"));
    }
}

TEST_F(KVStorePersistenceTest, PersistsClear) {
    {
        kvstore::Options opts;
        opts.persistence_path = wal_path_;
        kvstore::KVStore store(opts);

        store.put("key1", "value1");
        store.put("key2", "value2");
        store.clear();
        store.put("key3", "value3");
    }

    {
        kvstore::Options opts;
        opts.persistence_path = wal_path_;
        kvstore::KVStore store(opts);

        EXPECT_FALSE(store.contains("key1"));
        EXPECT_FALSE(store.contains("key2"));
        EXPECT_TRUE(store.contains("key3"));
    }
}

TEST_F(KVStorePersistenceTest, PersistsOverwrite) {
    {
        kvstore::Options opts;
        opts.persistence_path = wal_path_;
        kvstore::KVStore store(opts);

        store.put("key1", "value1");
        store.put("key1", "value2");
        store.put("key1", "value3");
    }

    {
        kvstore::Options opts;
        opts.persistence_path = wal_path_;
        kvstore::KVStore store(opts);

        auto result = store.get("key1");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, "value3");
    }
}

}  // namespace kvstore::test