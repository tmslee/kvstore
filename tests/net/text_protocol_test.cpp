#include "kvstore/net/text_protocol.hpp"

#include <gtest/gtest.h>

namespace kvstore::net::test {

TEST(TextProtocolTest, ParseSimpleCommand) {
    Request req = TextProtocol::decode_request("GET key1");
    EXPECT_EQ(req.command, Command::Get);
    EXPECT_EQ(req.key, "key1");
    EXPECT_EQ(req.value, "");
    EXPECT_EQ(req.ttl_ms, 0);
}

TEST(TextProtocolTest, ParseCommandWithMultipleArgs) {
    Request req = TextProtocol::decode_request("PUT key1 hello world");
    EXPECT_EQ(req.command, Command::Put);
    EXPECT_EQ(req.key, "key1");
    EXPECT_EQ(req.value, "hello world");
    EXPECT_EQ(req.ttl_ms, 0);
}

TEST(TextProtocolTest, ParseCommandCaseInsensitive) {
    Request req = TextProtocol::decode_request("get KEY1");
    EXPECT_EQ(req.command, Command::Get);
    EXPECT_EQ(req.key, "KEY1");
    EXPECT_EQ(req.value, "");
    EXPECT_EQ(req.ttl_ms, 0);
}

TEST(TextProtocolTest, ParseEmptyLine) {
    Request req = TextProtocol::decode_request("");
    EXPECT_EQ(req.command, Command::Unknown);
    EXPECT_EQ(req.key, "");
    EXPECT_EQ(req.value, "");
    EXPECT_EQ(req.ttl_ms, 0);
}

TEST(TextProtocolTest, ParseWhitespaceOnly) {
    Request req = TextProtocol::decode_request("   \t  ");
    EXPECT_EQ(req.command, Command::Unknown);
    EXPECT_EQ(req.key, "");
    EXPECT_EQ(req.value, "");
    EXPECT_EQ(req.ttl_ms, 0);
}

TEST(TextProtocolTest, ParseTrimsWhitespace) {
    Request req = TextProtocol::decode_request("  GET   key1  ");
    EXPECT_EQ(req.command, Command::Get);
    EXPECT_EQ(req.key, "key1");
    EXPECT_EQ(req.value, "");
    EXPECT_EQ(req.ttl_ms, 0);
}

TEST(TextProtocolTest, SerializeOk) {
    Response resp = Response::ok();
    EXPECT_EQ(TextProtocol::encode_response(resp), "OK\n");
}

TEST(TextProtocolTest, SerializeOkWithMessage) {
    Response resp = Response::ok("value123");
    EXPECT_EQ(TextProtocol::encode_response(resp), "OK value123\n");
}

TEST(TextProtocolTest, SerializeNotFound) {
    Response resp = Response::not_found();
    EXPECT_EQ(TextProtocol::encode_response(resp), "NOT_FOUND\n");
}

TEST(TextProtocolTest, SerializeError) {
    Response resp = Response::error("something went wrong");
    EXPECT_EQ(TextProtocol::encode_response(resp), "ERROR something went wrong\n");
}

TEST(TextProtocolTest, SerializeBye) {
    Response resp = Response::bye();
    EXPECT_EQ(TextProtocol::encode_response(resp), "BYE\n");
    EXPECT_TRUE(resp.close_connection);
}

TEST(TextProtocolTest, ParsePutEx) {
    Request req = TextProtocol::decode_request("PUTEX key1 1000 hello world");
    EXPECT_EQ(req.command, Command::PutEx);
    EXPECT_EQ(req.key, "key1");
    EXPECT_EQ(req.value, "hello world");
    EXPECT_EQ(req.ttl_ms, 1000);
}

TEST(TextProtocolTest, ParseSetEx) {
    Request req = TextProtocol::decode_request("SETEX key1 500 value");
    EXPECT_EQ(req.command, Command::PutEx);
    EXPECT_EQ(req.key, "key1");
    EXPECT_EQ(req.value, "value");
    EXPECT_EQ(req.ttl_ms, 500);
}

}  // namespace kvstore::net::test