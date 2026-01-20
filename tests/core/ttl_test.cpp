#include "kvstore/util/clock.hpp"
#include "kvstore/core/store.hpp"

#include <gtest/gtest.h>

namespace kvstore::core::test {

class TTLTest : public ::testing::Test{
protected:
    void SetUp() override {
        clock_ = std::make_shared<util::MockClock>();
        StoreOptions opts;
        opts.clock = clock_;
        store_ = std::make_unique<Store>(opts);
    }
    std::shared_ptr<util::MockClock> clock_;
    std::unique_ptr<Store> store_;
};

TEST_F(TTLTest, KeyExpiresAfterTTL){
    store_->put("key1", "value1", util::Duration(1000));

    auto result = store_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value1");

    clock_->advance(util::Duration(500));
    result = store_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value1");

    clock_->advance(util::Duration(600));
    result = store_->get("key1");
    EXPECT_FALSE(result.has_value());
}

TEST_F(TTLTest, ContainsReturnsFalseForExpired){
    store_->put("key1", "value1", util::Duration(1000));

    EXPECT_TRUE(store_->contains("key1"));

    clock_->advance(util::Duration(1001));

    EXPECT_FALSE(store_->contains("key1"));
}

TEST_F(TTLTest, KeyWithoutTTLNeverExpires){
    store_->put("key1", "value1");

    clock_->advance(util::Duration(1000000));

    auto result = store_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value1");
}

TEST_F(TTLTest, PutOverwritesTTL){
    store_->put("key1", "value1", util::Duration(1000));

    clock_->advance(util::Duration(500));

    store_->put("key1", "value2", util::Duration(2000));

    clock_->advance(util::Duration(1500));

    auto result = store_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value2");
}

TEST_F(TTLTest, PutWithoutTTLRemovesTTL){
    store_->put("key1", "value1", util::Duration(1000));

    clock_->advance(util::Duration(500));

    store_->put("key1", "value2");

    clock_->advance(util::Duration(1000));

    auto result = store_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value2");
}

TEST_F(TTLTest, CleanupExpiredRemovesExpiredKeys){
    store_->put("key1", "value1", util::Duration(1000));
    store_->put("key2", "value2", util::Duration(2000));
    store_->put("key3", "value3");

    clock_->advance(util::Duration(1500));

    store_->cleanup_expired();

    EXPECT_FALSE(store_->contains("key1"));
    EXPECT_TRUE(store_->contains("key2"));
    EXPECT_TRUE(store_->contains("key3"));
}

TEST_F(TTLTest, MultipleTTLs){
    store_->put("key1", "value1", util::Duration(100));
    store_->put("key2", "value2", util::Duration(200));
    store_->put("key3", "value3", util::Duration(300));

    clock_->advance(util::Duration(150));
    EXPECT_FALSE(store_->contains("key1"));
    EXPECT_TRUE(store_->contains("key2"));
    EXPECT_TRUE(store_->contains("key3"));

    clock_->advance(util::Duration(100));
    EXPECT_FALSE(store_->contains("key2"));
    EXPECT_TRUE(store_->contains("key3"));

    clock_->advance(util::Duration(100));
    EXPECT_FALSE(store_->contains("key3"));
}

} //namespace kvstore::core::test