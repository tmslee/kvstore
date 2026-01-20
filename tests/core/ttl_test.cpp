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

TEST_F(TTLTest, KeyExpiresAfterTTL){}

TEST_F(TTLTest, ContainsReturnsFalseForExpired){}

TEST_F(TTLTest, KeyWithoutTTLNeverExpires){}

TEST_F(TTLTest, PutOverwritesTTL){}

TEST_F(TTLTest, PutWithoutTTLRemovesTTL){}

TEST_F(TTLTest, CleanupExpiredRemovesExpiredKeys){}

TEST_F(TTLTest, MultipleTTLs){}

} //namespace kvstore::core::test