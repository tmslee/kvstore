#include "kvstore/net/protocol.hpp"

#include <gtest/gtest.h>

namespace kvstore::net::test {

TEST(ProtocolTest, ParseSimpleCommand) {
    auto cmd = Protocol::parse("GET key1");
    EXPECT_EQ(cmd.command, "GET");
    ASSERT_EQ(cmd.args.size(), 1);
    EXPECT_EQ(cmd.args[0], "key1");
}

TEST(ProtocolTest, ParseCommandWithMultipleArgs) {
    auto cmd = Protocol::parse("PUT key1 hello world");
    EXPECT_EQ(cmd.command, "PUT");
    ASSERT_EQ(cmd.args.size(), 3);
    EXPECT_EQ(cmd.args[0], "key1");
    EXPECT_EQ(cmd.args[1], "hello");
    EXPECT_EQ(cmd.args[2], "world");
}

TEST(ProtocolTest, ParseCommandCaseInsensitive) {
    auto cmd = Protocol::parse("get KEY1");
    EXPECT_EQ(cmd.command, "GET");
    EXPECT_EQ(cmd.args[0], "KEY1");
}

TEST(ProtocolTest, ParseEmptyLine) {
    auto cmd = Protocol::parse("");
    EXPECT_TRUE(cmd.command.empty());
    EXPECT_TRUE(cmd.args.empty());
}

TEST(ProtocolTest, ParseWhitespaceOnly) {
    auto cmd = Protocol::parse("   \t  ");
    EXPECT_TRUE(cmd.command.empty());
    EXPECT_TRUE(cmd.args.empty());
}

TEST(ProtocolTest, ParseTrimsWhitespace) {
    auto cmd = Protocol::parse("  GET   key1  ");
    EXPECT_EQ(cmd.command, "GET");
    ASSERT_EQ(cmd.args.size(), 1);
    EXPECT_EQ(cmd.args[0], "key1");
}

TEST(ProtocolTest, SerializeOk) {
    auto result = Protocol::ok();
    EXPECT_EQ(Protocol::serialize(result), "OK\n");
}

TEST(ProtocolTest, SerializeOkWithMessage) {
    auto result = Protocol::ok("value123");
    EXPECT_EQ(Protocol::serialize(result), "OK value123\n");
}

TEST(ProtocolTest, SerializeNotFound) {
    auto result = Protocol::not_found();
    EXPECT_EQ(Protocol::serialize(result), "NOT_FOUND\n");
}

TEST(ProtocolTest, SerializeError) {
    auto result = Protocol::error("something went wrong");
    EXPECT_EQ(Protocol::serialize(result), "ERROR something went wrong\n");
}

TEST(ProtocolTest, SerializeBye) {
    auto result = Protocol::bye();
    EXPECT_EQ(Protocol::serialize(result), "BYE\n");
    EXPECT_TRUE(result.close_connection);
}

TEST(ProtocolTest, OkDoesNotCloseConnection) {
    EXPECT_FALSE(Protocol::ok().close_connection);
    EXPECT_FALSE(Protocol::ok("msg").close_connection);
}

TEST(ProtocolTest, NotFoundDoesNotCloseConnection) {
    EXPECT_FALSE(Protocol::not_found().close_connection);
}

TEST(ProtocolTest, ErrorDoesNotCloseConnection) {
    EXPECT_FALSE(Protocol::error("err").close_connection);
}

}  // namespace kvstore::net::test