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

TEST_F(DiskStoreTest, InitiallyEmpty){}
TEST_F(DiskStoreTest, PutAndGet){}
TEST_F(DiskStoreTest, GetMissingKey){}
TEST_F(DiskStoreTest, PutOverwrites){}
TEST_F(DiskStoreTest, Contains){}
TEST_F(DiskStoreTest, Remove){}
TEST_F(DiskStoreTest, Clear){}
TEST_F(DiskStoreTest, Size){}
TEST_F(DiskStoreTest, PersistsAcrossRestarts){}
TEST_F(DiskStoreTest, PersistsRemove){}
TEST_F(DiskStoreTest, PersistsOverwrite){}
TEST_F(DiskStoreTest, LargeValues){}
TEST_F(DiskStoreTest, ManyKeys){}
TEST_F(DiskStoreTest, Compaction){}

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

TEST_F(DiskStoreTTLTest, KeyExpiresAfterTTL) {}
TEST_F(DiskStoreTTLTest, TTLPersistsAcrossRestart) {}
TEST_F(DiskStoreTTLTest, ExpiredKeysRemovedDuringCompaction) {}

}