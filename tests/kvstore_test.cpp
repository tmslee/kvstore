#include "kvstore/kvstore.hpp"

#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

namespace kvstore::test {

class KVStoreTest : public ::testing::Test {
protected:
    KVStore store;
};

TEST_F(KVStoreTest, InitiallyEmpty) {

}

TEST_F(KVStoreTest, PutAndGet) {

}

TEST_F(KVStoreTest, GetMissingKey) {
    
}

TEST_F(KVStoreTest, PutOverwrites) {
    
}

TEST_F(KVStoreTest, Contains) {
    
}

TEST_F(KVStoreTest, Remove) {
    
}

TEST_F(KVStoreTest, Clear) {
    
}

TEST_F(KVStoreTest, Size) {
    
}

TEST_F(KVStoreTest, EmptyKeyAndValue) {
    
}

TEST_F(KVStoreTest, ConcurrentWrites) {
    
}

TEST_F(KVStoreTest, ConcurrentReadsAndWrites) {
    
}

}