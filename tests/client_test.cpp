#include "kvstore/client.hpp"
#include "kvstore/kvstore.hpp"
#include "kvstore/server.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace kvstore::test {

class ClientTest : public ::testing:Test {
protected:
    void SetUp() override {}
    void TearDOwn() override {}

    std::unique_ptr<KVStore> store_;
    std::unique_ptr<Server> server_;
    std::unique_ptr<Client> client_;
};

TEST_F(ClientTest, Ping) {}

TEST_F(ClientTest, PutAndGet) {}

TEST_F(ClientTest, GetMissing) {}

TEST_F(ClientTest, PutOverwrites) {}

TEST_F(ClientTest, Remove) {}

TEST_F(ClientTest, Contains) {}

TEST_F(ClientTest, Size) {}

TEST_F(ClientTest, Clear) {}

TEST_F(ClientTest, PutValueWithSpaces) {}

TEST_F(ClientTest, MultipleOperations) {}

TEST_F(ClientTest, ConnectDisconnectReconnect) {}

} //namespace kvstore::test