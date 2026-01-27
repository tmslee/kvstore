#include "kvstore/net/text_protocol.hpp"

#include <gtest/gtest.h>

namespace kvstore::net::test {

TEST(TextProtocolTest, EncodeRequestGet) {
    Request req{Command::Get, "mykey", "", 0};
    EXPECT_EQ(TextProtocol::encode_request(req), "GET mykey\n");
}

TEST(TextProtocolTest, EncodeRequestPut) {
    Request req{Command::Put, "mykey", "myvalue", 0};
    EXPECT_EQ(TextProtocol::encode_request(req), "PUT mykey myvalue\n");
}

TEST(TextProtocolTest, EncodeRequestPutEx) {
    Request req{Command::PutEx, "mykey", "myvalue", 5000};
    EXPECT_EQ(TextProtocol::encode_request(req), "PUTEX mykey 5000 myvalue\n");
}

TEST(TextProtocolTest, EncodeRequestPing) {
    Request req{Command::Ping, "", "", 0};
    EXPECT_EQ(TextProtocol::encode_request(req), "PING\n");
}

TEST(TextProtocolTest, EncodeResponseOk) {
    Response resp{Status::Ok, "", false};
    EXPECT_EQ(TextProtocol::encode_response(resp), "OK\n");
}

TEST(TextProtocolTest, EncodeResponseOkWithData) {
    Response resp{Status::Ok, "value123", false};
    EXPECT_EQ(TextProtocol::encode_response(resp), "OK value123\n");
}

TEST(TextProtocolTest, EncodeResponseNotFound) {
    Response resp{Status::NotFound, "", false};
    EXPECT_EQ(TextProtocol::encode_response(resp), "NOT_FOUND\n");
}

TEST(TextProtocolTest, EncodeResponseError) {
    Response resp{Status::Error, "something went wrong", false};
    EXPECT_EQ(TextProtocol::encode_response(resp), "ERROR something went wrong\n");
}

TEST(TextProtocolTest, EncodeResponseBye) {
    Response resp{Status::Bye, "", true};
    EXPECT_EQ(TextProtocol::encode_response(resp), "BYE\n");
}

TEST(TextProtocolTest, DecodeRequestGet) {
    auto req = TextProtocol::decode_request("GET mykey");
    EXPECT_EQ(req.command, Command::Get);
    EXPECT_EQ(req.key, "mykey");
}

TEST(TextProtocolTest, DecodeRequestPut) {
    auto req = TextProtocol::decode_request("PUT mykey myvalue");
    EXPECT_EQ(req.command, Command::Put);
    EXPECT_EQ(req.key, "mykey");
    EXPECT_EQ(req.value, "myvalue");
}

TEST(TextProtocolTest, DecodeRequestPutWithSpaces) {
    auto req = TextProtocol::decode_request("PUT mykey hello world");
    EXPECT_EQ(req.command, Command::Put);
    EXPECT_EQ(req.key, "mykey");
    EXPECT_EQ(req.value, "hello world");
}

TEST(TextProtocolTest, DecodeRequestPutEx) {
    auto req = TextProtocol::decode_request("PUTEX mykey 5000 myvalue");
    EXPECT_EQ(req.command, Command::PutEx);
    EXPECT_EQ(req.key, "mykey");
    EXPECT_EQ(req.ttl_ms, 5000);
    EXPECT_EQ(req.value, "myvalue");
}

TEST(TextProtocolTest, DecodeRequestCaseInsensitive) {
    auto req = TextProtocol::decode_request("get mykey");
    EXPECT_EQ(req.command, Command::Get);

    req = TextProtocol::decode_request("GeT mykey");
    EXPECT_EQ(req.command, Command::Get);
}

TEST(TextProtocolTest, DecodeRequestPutExInvalidTTL) {
    auto req = TextProtocol::decode_request("PUTEX mykey notanumber myvalue");
    EXPECT_EQ(req.command, Command::Unknown);
}

TEST(TextProtocolTest, DecodeRequestPutExMissingArgs) {
    auto req = TextProtocol::decode_request("PUTEX mykey");
    EXPECT_EQ(req.command, Command::Unknown);

    req = TextProtocol::decode_request("PUTEX mykey 1000");
    EXPECT_EQ(req.command, Command::Unknown);

    req = TextProtocol::decode_request("PUTEX");
    EXPECT_EQ(req.command, Command::Unknown);
}

TEST(TextProtocolTest, DecodeRequestGetMissingKey) {
    auto req = TextProtocol::decode_request("GET");
    EXPECT_EQ(req.command, Command::Unknown);
}

TEST(TextProtocolTest, DecodeRequestPutMissingValue) {
    auto req = TextProtocol::decode_request("PUT mykey");
    EXPECT_EQ(req.command, Command::Unknown);
}

TEST(TextProtocolTest, DecodeRequestDelMissingKey) {
    auto req = TextProtocol::decode_request("DEL");
    EXPECT_EQ(req.command, Command::Unknown);
}

TEST(TextProtocolTest, DecodeRequestExistsMissingKey) {
    auto req = TextProtocol::decode_request("EXISTS");
    EXPECT_EQ(req.command, Command::Unknown);
}

TEST(TextProtocolTest, DecodeRequestAliases) {
    EXPECT_EQ(TextProtocol::decode_request("SET k v").command, Command::Put);
    EXPECT_EQ(TextProtocol::decode_request("SETEX k 100 v").command, Command::PutEx);
    EXPECT_EQ(TextProtocol::decode_request("DELETE k").command, Command::Del);
    EXPECT_EQ(TextProtocol::decode_request("REMOVE k").command, Command::Del);
    EXPECT_EQ(TextProtocol::decode_request("CONTAINS k").command, Command::Exists);
    EXPECT_EQ(TextProtocol::decode_request("COUNT").command, Command::Size);
    EXPECT_EQ(TextProtocol::decode_request("EXIT").command, Command::Quit);
}

TEST(TextProtocolTest, DecodeRequestUnknown) {
    auto req = TextProtocol::decode_request("INVALID command");
    EXPECT_EQ(req.command, Command::Unknown);
}

TEST(TextProtocolTest, DecodeRequestEmpty) {
    auto req = TextProtocol::decode_request("");
    EXPECT_EQ(req.command, Command::Unknown);
}

TEST(TextProtocolTest, DecodeResponseOk) {
    auto resp = TextProtocol::decode_response("OK");
    EXPECT_EQ(resp.status, Status::Ok);
    EXPECT_TRUE(resp.data.empty());
}

TEST(TextProtocolTest, DecodeResponseOkWithData) {
    auto resp = TextProtocol::decode_response("OK myvalue");
    EXPECT_EQ(resp.status, Status::Ok);
    EXPECT_EQ(resp.data, "myvalue");
}

TEST(TextProtocolTest, DecodeResponseNotFound) {
    auto resp = TextProtocol::decode_response("NOT_FOUND");
    EXPECT_EQ(resp.status, Status::NotFound);
}

TEST(TextProtocolTest, DecodeResponseError) {
    auto resp = TextProtocol::decode_response("ERROR something bad");
    EXPECT_EQ(resp.status, Status::Error);
    EXPECT_EQ(resp.data, "something bad");
}

TEST(TextProtocolTest, DecodeResponseBye) {
    auto resp = TextProtocol::decode_response("BYE");
    EXPECT_EQ(resp.status, Status::Bye);
    EXPECT_TRUE(resp.close_connection);
}

TEST(TextProtocolTest, CommandToString) {
    EXPECT_EQ(TextProtocol::command_to_string(Command::Get), "GET");
    EXPECT_EQ(TextProtocol::command_to_string(Command::Put), "PUT");
    EXPECT_EQ(TextProtocol::command_to_string(Command::PutEx), "PUTEX");
    EXPECT_EQ(TextProtocol::command_to_string(Command::Del), "DEL");
    EXPECT_EQ(TextProtocol::command_to_string(Command::Exists), "EXISTS");
    EXPECT_EQ(TextProtocol::command_to_string(Command::Size), "SIZE");
    EXPECT_EQ(TextProtocol::command_to_string(Command::Clear), "CLEAR");
    EXPECT_EQ(TextProtocol::command_to_string(Command::Ping), "PING");
    EXPECT_EQ(TextProtocol::command_to_string(Command::Quit), "QUIT");
    EXPECT_EQ(TextProtocol::command_to_string(Command::Unknown), "UNKNOWN");
}

TEST(TextProtocolTest, ParseCommand) {
    EXPECT_EQ(TextProtocol::parse_command("GET"), Command::Get);
    EXPECT_EQ(TextProtocol::parse_command("get"), Command::Get);
    EXPECT_EQ(TextProtocol::parse_command("PUT"), Command::Put);
    EXPECT_EQ(TextProtocol::parse_command("SET"), Command::Put);
    EXPECT_EQ(TextProtocol::parse_command("INVALID"), Command::Unknown);
}

}  // namespace kvstore::net::test