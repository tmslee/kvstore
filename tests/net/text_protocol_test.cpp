#include "kvstore/net/text_protocol.hpp"

#include <gtest/gtest.h>

namespace kvstore::net::test {

TEST(TextProtocolTest, ParseSimpleCommand) {
    auto cmd = TextProtocol::parse("GET key1");
    EXPECT_EQ(cmd.command, "GET");
    ASSERT_EQ(cmd.args.size(), 1);
    EXPECT_EQ(cmd.args[0], "key1");
}

TEST(TextProtocolTest, ParseCommandWithMultipleArgs) {
    auto cmd = TextProtocol::parse("PUT key1 hello world");
    EXPECT_EQ(cmd.command, "PUT");
    ASSERT_EQ(cmd.args.size(), 3);
    EXPECT_EQ(cmd.args[0], "key1");
    EXPECT_EQ(cmd.args[1], "hello");
    EXPECT_EQ(cmd.args[2], "world");
}

TEST(TextProtocolTest, ParseCommandCaseInsensitive) {
    auto cmd = TextProtocol::parse("get KEY1");
    EXPECT_EQ(cmd.command, "GET");
    EXPECT_EQ(cmd.args[0], "KEY1");
}

TEST(TextProtocolTest, ParseEmptyLine) {
    auto cmd = TextProtocol::parse("");
    EXPECT_TRUE(cmd.command.empty());
    EXPECT_TRUE(cmd.args.empty());
}

TEST(TextProtocolTest, ParseWhitespaceOnly) {
    auto cmd = TextProtocol::parse("   \t  ");
    EXPECT_TRUE(cmd.command.empty());
    EXPECT_TRUE(cmd.args.empty());
}

TEST(TextProtocolTest, ParseTrimsWhitespace) {
    auto cmd = TextProtocol::parse("  GET   key1  ");
    EXPECT_EQ(cmd.command, "GET");
    ASSERT_EQ(cmd.args.size(), 1);
    EXPECT_EQ(cmd.args[0], "key1");
}

TEST(TextProtocolTest, SerializeOk) {
    auto result = TextProtocol::ok();
    EXPECT_EQ(TextProtocol::serialize(result), "OK\n");
}

TEST(TextProtocolTest, SerializeOkWithMessage) {
    auto result = TextProtocol::ok("value123");
    EXPECT_EQ(TextProtocol::serialize(result), "OK value123\n");
}

TEST(TextProtocolTest, SerializeNotFound) {
    auto result = TextProtocol::not_found();
    EXPECT_EQ(TextProtocol::serialize(result), "NOT_FOUND\n");
}

TEST(TextProtocolTest, SerializeError) {
    auto result = TextProtocol::error("something went wrong");
    EXPECT_EQ(TextProtocol::serialize(result), "ERROR something went wrong\n");
}

TEST(TextProtocolTest, SerializeBye) {
    auto result = TextProtocol::bye();
    EXPECT_EQ(TextProtocol::serialize(result), "BYE\n");
    EXPECT_TRUE(result.close_connection);
}

TEST(TextProtocolTest, OkDoesNotCloseConnection) {
    EXPECT_FALSE(TextProtocol::ok().close_connection);
    EXPECT_FALSE(TextProtocol::ok("msg").close_connection);
}

TEST(TextProtocolTest, NotFoundDoesNotCloseConnection) {
    EXPECT_FALSE(TextProtocol::not_found().close_connection);
}

TEST(TextProtocolTest, ErrorDoesNotCloseConnection) {
    EXPECT_FALSE(TextProtocol::error("err").close_connection);
}

TEST(TextProtocolTest, ParsePutEx) {
    auto cmd = TextProtocol::parse("PUTEX key1 1000 hello world");
    EXPECT_EQ(cmd.command, "PUTEX");
    ASSERT_EQ(cmd.args.size(), 4);
    EXPECT_EQ(cmd.args[0], "key1");
    EXPECT_EQ(cmd.args[1], "1000");
    EXPECT_EQ(cmd.args[2], "hello");
    EXPECT_EQ(cmd.args[3], "world");
}

TEST(TextProtocolTest, ParseSetEx) {
    auto cmd = TextProtocol::parse("SETEX key1 500 value");
    EXPECT_EQ(cmd.command, "SETEX");
    ASSERT_EQ(cmd.args.size(), 3);
    EXPECT_EQ(cmd.args[0], "key1");
    EXPECT_EQ(cmd.args[1], "500");
    EXPECT_EQ(cmd.args[2], "value");
}

}  // namespace kvstore::net::test