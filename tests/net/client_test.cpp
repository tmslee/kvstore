#include "kvstore/net/client.hpp"

#include <gtest/gtest.h>

#include <memory>

#include "kvstore/core/store.hpp"
#include "kvstore/net/server.hpp"

namespace kvstore::net::test {

class ClientTest : public ::testing::Test {
   protected:
    void SetUp() override {
        store_ = std::make_unique<core::Store>();
        ServerOptions server_opts;
        server_opts.port = 16380;
        server_ = std::make_unique<Server>(*store_, server_opts);
        server_->start();

        ClientOptions client_opts;
        client_opts.port = 16380;
        client_opts.timeout_seconds = 5;
        client_ = std::make_unique<Client>(client_opts);
        client_->connect();
    }

    void TearDown() override {
        client_->disconnect();
        server_->stop();
    }

    std::unique_ptr<core::Store> store_;
    std::unique_ptr<Server> server_;
    std::unique_ptr<Client> client_;
};

TEST_F(ClientTest, Ping) {
    EXPECT_TRUE(client_->ping());
}

TEST_F(ClientTest, PutAndGet) {
    client_->put("key1", "value1");

    auto result = client_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value1");
}

TEST_F(ClientTest, GetMissing) {
    auto result = client_->get("nonexistent");
    EXPECT_FALSE(result.has_value());
}

TEST_F(ClientTest, PutOverwrites) {
    client_->put("key1", "value1");
    client_->put("key1", "value2");

    auto result = client_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value2");
}

TEST_F(ClientTest, Remove) {
    client_->put("key1", "value1");

    EXPECT_TRUE(client_->remove("key1"));
    EXPECT_FALSE(client_->contains("key1"));
    EXPECT_FALSE(client_->remove("key1"));
}

TEST_F(ClientTest, Contains) {
    EXPECT_FALSE(client_->contains("key1"));

    client_->put("key1", "value1");
    EXPECT_TRUE(client_->contains("key1"));
}

TEST_F(ClientTest, Size) {
    EXPECT_EQ(client_->size(), 0);

    client_->put("key1", "value1");
    EXPECT_EQ(client_->size(), 1);

    client_->put("key2", "value2");
    EXPECT_EQ(client_->size(), 2);
}

TEST_F(ClientTest, Clear) {
    client_->put("key1", "value1");
    client_->put("key2", "value2");

    client_->clear();
    EXPECT_EQ(client_->size(), 0);
}

TEST_F(ClientTest, PutValueWithSpaces) {
    client_->put("key1", "hello world");

    auto result = client_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "hello world");
}

TEST_F(ClientTest, MultipleOperations) {
    for (int i = 0; i < 100; ++i) {
        client_->put("key" + std::to_string(i), "value" + std::to_string(i));
    }

    EXPECT_EQ(client_->size(), 100);

    for (int i = 0; i < 100; ++i) {
        auto result = client_->get("key" + std::to_string(i));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, "value" + std::to_string(i));
    }
}

TEST_F(ClientTest, ConnectDisconnectReconnect) {
    client_->put("key1", "value1");
    client_->disconnect();

    EXPECT_FALSE(client_->connected());

    client_->connect();
    EXPECT_TRUE(client_->connected());

    auto result = client_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "value1");
}

}  // namespace kvstore::net::test