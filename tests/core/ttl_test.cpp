#include <gtest/gtest.h>

#include <filesystem>

#include "kvstore/core/store.hpp"
#include "kvstore/util/clock.hpp"
#include "kvstore/util/types.hpp"

namespace kvstore::core::test {

using util::Duration;
using util::MockClock;

class TTLTest : public ::testing::Test {
   protected:
    void SetUp() override {
        clock_ = std::make_shared<MockClock>();
        StoreOptions opts;
        opts.clock = clock_;
        store_ = std::make_unique<Store>(opts);
    }
    std::shared_ptr<MockClock> clock_;
    std::unique_ptr<Store> store_;
};

TEST_F(TTLTest, KeyExpiresAfterTTL) {
    store_->put("key1", "value1", Duration(1000));

    auto result = store_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value1");

    clock_->advance(Duration(500));
    result = store_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value1");

    clock_->advance(Duration(600));
    result = store_->get("key1");
    EXPECT_FALSE(result.has_value());
}

TEST_F(TTLTest, ContainsReturnsFalseForExpired) {
    store_->put("key1", "value1", Duration(1000));

    EXPECT_TRUE(store_->contains("key1"));

    clock_->advance(Duration(1001));

    EXPECT_FALSE(store_->contains("key1"));
}

TEST_F(TTLTest, KeyWithoutTTLNeverExpires) {
    store_->put("key1", "value1");

    clock_->advance(Duration(1000000));

    auto result = store_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value1");
}

TEST_F(TTLTest, PutOverwritesTTL) {
    store_->put("key1", "value1", Duration(1000));

    clock_->advance(Duration(500));

    store_->put("key1", "value2", Duration(2000));

    clock_->advance(Duration(1500));

    auto result = store_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value2");
}

TEST_F(TTLTest, PutWithoutTTLRemovesTTL) {
    store_->put("key1", "value1", Duration(1000));

    clock_->advance(Duration(500));

    store_->put("key1", "value2");

    clock_->advance(Duration(1000));

    auto result = store_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value2");
}

TEST_F(TTLTest, CleanupExpiredRemovesExpiredKeys) {
    store_->put("key1", "value1", Duration(1000));
    store_->put("key2", "value2", Duration(2000));
    store_->put("key3", "value3");

    clock_->advance(Duration(1500));

    store_->cleanup_expired();

    EXPECT_FALSE(store_->contains("key1"));
    EXPECT_TRUE(store_->contains("key2"));
    EXPECT_TRUE(store_->contains("key3"));
}

TEST_F(TTLTest, MultipleTTLs) {
    store_->put("key1", "value1", Duration(100));
    store_->put("key2", "value2", Duration(200));
    store_->put("key3", "value3", Duration(300));

    clock_->advance(Duration(150));
    EXPECT_FALSE(store_->contains("key1"));
    EXPECT_TRUE(store_->contains("key2"));
    EXPECT_TRUE(store_->contains("key3"));

    clock_->advance(Duration(100));
    EXPECT_FALSE(store_->contains("key2"));
    EXPECT_TRUE(store_->contains("key3"));

    clock_->advance(Duration(100));
    EXPECT_FALSE(store_->contains("key3"));
}

TEST_F(TTLTest, TTLPersistsAcrossRestart) {
    auto test_dir = std::filesystem::temp_directory_path() / "ttl_persist_test";
    std::filesystem::create_directories(test_dir);
    auto wal_path = test_dir / "test.wal";

    auto shared_clock = std::make_shared<MockClock>();
    {
        StoreOptions opts;
        opts.persistence_path = wal_path;
        opts.clock = shared_clock;
        Store store(opts);

        store.put("key1", "value1", Duration(10000));
        store.put("key2", "value2");
    }

    shared_clock->advance(Duration(5000));

    {
        StoreOptions opts;
        opts.persistence_path = wal_path;
        opts.clock = shared_clock;
        Store store(opts);

        EXPECT_TRUE(store.contains("key1"));
        EXPECT_TRUE(store.contains("key2"));

        shared_clock->advance(Duration(6000));

        EXPECT_FALSE(store.contains("key1"));
        EXPECT_TRUE(store.contains("key2"));
    }

    std::filesystem::remove_all(test_dir);
}

TEST_F(TTLTest, ExpiredKeyNotLoadedOnRecovery) {
    auto test_dir = std::filesystem::temp_directory_path() / "ttl_expired_test";
    std::filesystem::create_directories(test_dir);
    auto wal_path = test_dir / "test.wal";

    auto shared_clock = std::make_shared<MockClock>();

    {
        StoreOptions opts;
        opts.persistence_path = wal_path;
        opts.clock = shared_clock;
        Store store(opts);

        store.put("key1", "value1", Duration(1000));
    }

    {
        shared_clock->advance(Duration(2000));
        StoreOptions opts;
        opts.persistence_path = wal_path;
        opts.clock = shared_clock;
        Store store(opts);

        EXPECT_FALSE(store.contains("key1"));
    }

    std::filesystem::remove_all(test_dir);
}

}  // namespace kvstore::core::test