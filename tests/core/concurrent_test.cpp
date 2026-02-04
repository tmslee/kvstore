#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <random>
#include <thread>
#include <vector>

#include "kvstore/core/disk_store.hpp"
#include "kvstore/core/store.hpp"

namespace kvstore::core::test {

// =============================================================================
// Store Concurrent Tests
// =============================================================================

class StoreConcurrentTest : public ::testing::Test {
   protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "store_concurrent_test";
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);

        StoreOptions opts;
        opts.persistence_path = test_dir_ / "test.wal";
        opts.snapshot_path = test_dir_ / "test.snap";
        store_ = std::make_unique<Store>(opts);
    }

    void TearDown() override {
        store_.reset();
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::unique_ptr<Store> store_;
};

TEST_F(StoreConcurrentTest, ConcurrentPutGet) {
    constexpr int kNumThreads = 4;
    constexpr int kOpsPerThread = 1000;

    std::atomic<int> errors{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([this, t, &errors]() {
            for (int i = 0; i < kOpsPerThread; ++i) {
                std::string key = "key_" + std::to_string(t) + "_" + std::to_string(i);
                std::string value = "value_" + std::to_string(t) + "_" + std::to_string(i);

                store_->put(key, value);
                auto result = store_->get(key);

                if (!result.has_value() || *result != value) {
                    ++errors;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(store_->size(), kNumThreads * kOpsPerThread);
}

TEST_F(StoreConcurrentTest, ConcurrentPutRemove) {
    constexpr int kNumThreads = 4;
    constexpr int kOpsPerThread = 500;

    std::atomic<int> put_count{0};
    std::atomic<int> remove_count{0};
    std::vector<std::thread> threads;

    // Half threads do puts, half do removes
    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([this, t, &put_count, &remove_count]() {
            std::mt19937 rng(t);
            for (int i = 0; i < kOpsPerThread; ++i) {
                std::string key = "key_" + std::to_string(rng() % 100);
                if (t % 2 == 0) {
                    store_->put(key, "value");
                    ++put_count;
                } else {
                    (void)store_->remove(key);
                    ++remove_count;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Just verify no crashes - exact count depends on timing
    EXPECT_GT(put_count.load(), 0);
    EXPECT_GT(remove_count.load(), 0);
}

TEST_F(StoreConcurrentTest, ConcurrentReadWrite) {
    // Pre-populate some data
    for (int i = 0; i < 100; ++i) {
        store_->put("key_" + std::to_string(i), "initial_value");
    }

    constexpr int kNumReaders = 4;
    constexpr int kNumWriters = 2;
    constexpr int kOpsPerThread = 1000;

    std::atomic<bool> stop{false};
    std::atomic<int> read_count{0};
    std::atomic<int> write_count{0};
    std::vector<std::thread> threads;

    // Reader threads
    for (int t = 0; t < kNumReaders; ++t) {
        threads.emplace_back([this, &stop, &read_count]() {
            std::mt19937 rng(std::random_device{}());
            while (!stop.load()) {
                int key_id = rng() % 100;
                auto result = store_->get("key_" + std::to_string(key_id));
                ++read_count;
            }
        });
    }

    // Writer threads
    for (int t = 0; t < kNumWriters; ++t) {
        threads.emplace_back([this, t, &write_count]() {
            for (int i = 0; i < kOpsPerThread; ++i) {
                std::string key = "key_" + std::to_string(i % 100);
                std::string value = "value_" + std::to_string(t) + "_" + std::to_string(i);
                store_->put(key, value);
                ++write_count;
            }
        });
    }

    // Wait for writers to finish
    for (int t = kNumReaders; t < kNumReaders + kNumWriters; ++t) {
        threads[t].join();
    }

    // Stop readers
    stop.store(true);
    for (int t = 0; t < kNumReaders; ++t) {
        threads[t].join();
    }

    EXPECT_EQ(write_count.load(), kNumWriters * kOpsPerThread);
    EXPECT_GT(read_count.load(), 0);
}

// =============================================================================
// DiskStore Concurrent Tests
// =============================================================================

class DiskStoreConcurrentTest : public ::testing::Test {
   protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "disk_store_concurrent_test";
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);

        DiskStoreOptions opts;
        opts.data_dir = test_dir_;
        // Low threshold to trigger auto-compaction during concurrent tests
        opts.compaction_threshold = 50;
        store_ = std::make_unique<DiskStore>(opts);
    }

    void TearDown() override {
        store_.reset();
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::unique_ptr<DiskStore> store_;
};

TEST_F(DiskStoreConcurrentTest, ConcurrentPutGet) {
    constexpr int kNumThreads = 4;
    constexpr int kOpsPerThread = 500;

    std::atomic<int> errors{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([this, t, &errors]() {
            for (int i = 0; i < kOpsPerThread; ++i) {
                std::string key = "key_" + std::to_string(t) + "_" + std::to_string(i);
                std::string value = "value_" + std::to_string(t) + "_" + std::to_string(i);

                store_->put(key, value);
                auto result = store_->get(key);

                if (!result.has_value() || *result != value) {
                    ++errors;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(store_->size(), kNumThreads * kOpsPerThread);
}

TEST_F(DiskStoreConcurrentTest, ConcurrentWithAutoCompaction) {
    constexpr int kNumThreads = 4;
    constexpr int kOpsPerThread = 200;

    std::vector<std::thread> threads;

    // All threads write to overlapping key space to trigger tombstones and auto-compaction
    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([this, t]() {
            for (int i = 0; i < kOpsPerThread; ++i) {
                // Use shared key space to create overwrites/deletes
                std::string key = "shared_key_" + std::to_string(i % 50);
                std::string value = "value_" + std::to_string(t) + "_" + std::to_string(i);

                if (i % 3 == 0) {
                    store_->put(key, value);
                } else if (i % 3 == 1) {
                    (void)store_->remove(key);
                } else {
                    auto result = store_->get(key);
                    // Result may or may not exist depending on timing
                    (void)result;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Verify store is still consistent after concurrent operations
    EXPECT_NO_THROW({
        for (int i = 0; i < 50; ++i) {
            std::string key = "shared_key_" + std::to_string(i);
            auto result = store_->get(key);
            if (result.has_value()) {
                EXPECT_FALSE(result->empty());
            }
        }
    });

    // Manual compaction after concurrent operations should work
    EXPECT_NO_THROW(store_->compact());
}

TEST_F(DiskStoreConcurrentTest, ConcurrentReadWrite) {
    // Pre-populate
    for (int i = 0; i < 50; ++i) {
        store_->put("key_" + std::to_string(i), "initial_value");
    }

    constexpr int kNumReaders = 4;
    constexpr int kNumWriters = 2;
    constexpr int kOpsPerWriter = 500;

    std::atomic<bool> stop{false};
    std::atomic<int> read_count{0};
    std::vector<std::thread> threads;

    // Reader threads
    for (int t = 0; t < kNumReaders; ++t) {
        threads.emplace_back([this, &stop, &read_count]() {
            std::mt19937 rng(std::random_device{}());
            while (!stop.load()) {
                int key_id = rng() % 50;
                auto result = store_->get("key_" + std::to_string(key_id));
                ++read_count;
            }
        });
    }

    // Writer threads
    for (int t = 0; t < kNumWriters; ++t) {
        threads.emplace_back([this, t]() {
            for (int i = 0; i < kOpsPerWriter; ++i) {
                std::string key = "key_" + std::to_string(i % 50);
                std::string value = "value_" + std::to_string(t) + "_" + std::to_string(i);
                store_->put(key, value);
            }
        });
    }

    // Wait for writers
    for (size_t t = kNumReaders; t < threads.size(); ++t) {
        threads[t].join();
    }

    // Stop readers
    stop.store(true);
    for (int t = 0; t < kNumReaders; ++t) {
        threads[t].join();
    }

    EXPECT_GT(read_count.load(), 0);
}

}  // namespace kvstore::core::test
