#include "kvstore/core/disk_store.hpp"
#include "kvstore/util/clock.hpp"
#include "kvstore/util/types.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace kvstore::core::test {

namespace util = kvstore::util;

class DiskStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directorY_path() / "disk_store_test";
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);

        DiskStoreOPtions opts;
        opts.data_dir = test_dir_;
        store_ = std::make_unique<DiskStore>(opts);
    }

    void TearDown() override {
        store_.reset();
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::unique_ptr<DiskStore> store_;
}

TEST_F(DiskStoreTest, InitiallyEmpty){
    EXPECT_TRUE(store_->empty());
    EXPECT_EQ(store_->size(), 0);
}
TEST_F(DiskStoreTest, PutAndGet){
    store_->put("key1", "value1");

    auto result = store_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value1");
}

TEST_F(DiskStoreTest, GetMissingKey){
    auto result = store_->get("nonexistent");
    EXPECT_FALSE(result.has_value());
}

TEST_F(DiskStoreTest, PutOverwrites){
    store_->put("key1", "value1");
    store_->put("key1", "value2");

    auto result = store_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value2");
}

TEST_F(DiskStoreTest, Contains){
    EXPECT_FALSE(store_->contains("key1"));

    store_->put("key1", "value1");
    EXPECT_TRUE(store_->contains("key1"));
}

TEST_F(DiskStoreTest, Remove){
    store_->put("key1", "value1");

    EXPECT_TRUE(store_->remove("key1"));
    EXPECT_FALSE(store_->contains("key1"));
    EXPECT_FALSE(store_->remove("key1"));
}

TEST_F(DiskStoreTest, Clear){
    store_->put("key1", "value1");
    store_->put("key2", "value2");
    store_->put("key3", "value3");

    store_->clear();

    EXPECT_TRUE(store_->empty());
    EXPECT_EQ(store_->size(), 0);
    EXPECT_FALSE(store_->contains("key1"));
}

TEST_F(DiskStoreTest, Size){
    EXPECT_EQ(store_->size(), 0);

    store_->put("key1", "value1");
    EXPECT_EQ(store_->size(), 1);

    store_->put("key2", "value2");
    EXPECT_EQ(store_->size(), 2);

    store_->put("key1", "newvalue");
    EXPECT_EQ(store_->size(), 2);

    store_->remove("key1");
    EXPECT_EQ(store_->size(), 1);
}

TEST_F(DiskStoreTest, PersistsAcrossRestarts){
    store_->put("key1", "value1");
    store_->put("key2", "value2");

    store_.reset();

    DiskStoreOptions opts;
    opts.data_dir = test_dir_;
    store_ = std::make_unique<DiskStore>(opts);

    auto result1 = store_->get("key1");
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(*result1, "value1");

    auto result2 = store_->get("key2");
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(*result2, "value2");
}

TEST_F(DiskStoreTest, PersistsRemove){
    store_->put("key1", "value1");
    store_->put("key2", "value2");
    store_->remove("key1");

    store_.reset();

    DiskStoreOptions opts;
    opts.data_dir = test_dir_;
    store_ = std::make_unique<DiskStore>(opts);

    EXPECT_FALSE(store_->contains("key1"));
    EXPECT_TRUE(store_->contains("key2"));
}

TEST_F(DiskStoreTest, PersistsOverwrite){
        store_->put("key1", "value1");
    store_->put("key1", "value2");
    store_->put("key1", "value3");

    store_.reset();

    DiskStoreOptions opts;
    opts.data_dir = test_dir_;
    store_ = std::make_unique<DiskStore>(opts);

    auto result = store_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value3");
}

TEST_F(DiskStoreTest, LargeValues){
    std::string large_value(100000, 'x');
    store_->put("large", large_value);

    auto result = store_->get("large");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, large_value);
}

TEST_F(DiskStoreTest, ManyKeys){
    for (int i = 0; i < 1000; ++i) {
        store_->put("key" + std::to_string(i), "value" + std::to_string(i));
    }

    EXPECT_EQ(store_->size(), 1000);

    for (int i = 0; i < 1000; ++i) {
        auto result = store_->get("key" + std::to_string(i));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, "value" + std::to_string(i));
    }
}

TEST_F(DiskStoreTest, Compaction){
    DiskStoreOptions opts;
    opts.data_dir = test_dir_;
    opts.compaction_threshold = 10;
    store_ = std::make_unique<DiskStore>(opts);

    for (int i = 0; i < 20; ++i) {
        store_->put("key", "value" + std::to_string(i));
    }

    for (int i = 0; i < 15; ++i) {
        store_->put("temp" + std::to_string(i), "value");
        store_->remove("temp" + std::to_string(i));
    }

    // After enough tombstones, compaction should happen
    auto result = store_->get("key");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value19");
}

class DiskStoreTTLTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "disk_store_ttl_test";
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);

        clock_ = std::make_shared<util::MockClock>();

        DiskStoreOptions opts;
        opts.data_dir = test_dir_;
        opts.clock = clock_;
        store_ = std::make_unique<DiskStore>(opts);
    }

    void TearDown() override {
        store_.reset();
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::shared_ptr<util::MockClock> clock_;
    std::unique_ptr<DiskStore> store_;
}

TEST_F(DiskStoreTTLTest, KeyExpiresAfterTTL) {
    store_->put("key1", "value1", Duration(1000));

    auto result = store_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value1");

    clock_->advance(Duration(1100));

    result = store_->get("key1");
    EXPECT_FALSE(result.has_value());
}

TEST_F(DiskStoreTTLTest, TTLPersistsAcrossRestart) {
    store_->put("key1", "value1", Duration(10000));
    store_->put("key2", "value2");

    store_.reset();

    clock_->advance(Duration(5000));

    DiskStoreOptions opts;
    opts.data_dir = test_dir_;
    opts.clock = clock_;
    store_ = std::make_unique<DiskStore>(opts);

    EXPECT_TRUE(store_->contains("key1"));
    EXPECT_TRUE(store_->contains("key2"));

    clock_->advance(Duration(6000));

    EXPECT_FALSE(store_->contains("key1"));
    EXPECT_TRUE(store_->contains("key2"));
}

TEST_F(DiskStoreTTLTest, ExpiredKeysRemovedDuringCompaction) {
    DiskStoreOptions opts;
    opts.data_dir = test_dir_;
    opts.clock = clock_;
    opts.compaction_threshold = 5;
    store_ = std::make_unique<DiskStore>(opts);

    store_->put("expiring", "value", Duration(1000));
    store_->put("permanent", "value");

    clock_->advance(Duration(2000));

    // Trigger compaction
    for (int i = 0; i < 10; ++i) {
        store_->put("temp" + std::to_string(i), "value");
        store_->remove("temp" + std::to_string(i));
    }

    // After compaction, expired key should be gone
    EXPECT_FALSE(store_->contains("expiring"));
    EXPECT_TRUE(store_->contains("permanent"));
}

}