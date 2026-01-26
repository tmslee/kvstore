#include "kvstore/net/binary_protocol.hpp"

#include <gtest/gtest.h>

namespace kvstore::net::test {

TEST(BinaryProtocolTest, EncodeDecodeGet) {
    Request req;
    req.command = Command::Get;
    req.key = "mykey";

    auto encoded = BinaryProtocol::encode_request(req);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->command, Command::Get);
    EXPECT_EQ(decoded->key, "mykey");
    EXPECT_EQ(consumed, encoded.size());
}

TEST(BinaryProtocolTest, EncodeDecodePut) {
    Request req;
    req.command = Command::Put;
    req.key = "mykey";
    req.value = "myvalue";

    auto encoded = BinaryProtocol::encode_request(req);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->command, Command::Put);
    EXPECT_EQ(decoded->key, "mykey");
    EXPECT_EQ(decoded->value, "myvalue");
}

TEST(BinaryProtocolTest, EncodeDecodePutEx) {
    Request req;
    req.command = Command::PutEx;
    req.key = "mykey";
    req.value = "myvalue";
    req.ttl_ms = 60000;

    auto encoded = BinaryProtocol::encode_request(req);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->command, Command::PutEx);
    EXPECT_EQ(decoded->key, "mykey");
    EXPECT_EQ(decoded->value, "myvalue");
    EXPECT_EQ(decoded->ttl_ms, 60000);
}

TEST(BinaryProtocolTest, EncodeDecodePing) {
    Request req;
    req.command = Command::Ping;

    auto encoded = BinaryProtocol::encode_request(req);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->command, Command::Ping);
}

TEST(BinaryProtocolTest, EncodeDecodeResponseOk) {
    BinaryResponse resp;
    resp.status = BinaryStatus::Ok;
    resp.data = "PONG";

    auto encoded = BinaryProtocol::encode_response(resp);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_response(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->status, BinaryStatus::Ok);
    EXPECT_EQ(decoded->data, "PONG");
    EXPECT_FALSE(decoded->close_connection);
}

TEST(BinaryProtocolTest, EncodeDecodeResponseNotFound) {
    BinaryResponse resp;
    resp.status = BinaryStatus::NotFound;

    auto encoded = BinaryProtocol::encode_response(resp);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_response(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->status, BinaryStatus::NotFound);
    EXPECT_TRUE(decoded->data.empty());
}

TEST(BinaryProtocolTest, EncodeDecodeResponseBye) {
    BinaryResponse resp;
    resp.status = BinaryStatus::Bye;

    auto encoded = BinaryProtocol::encode_response(resp);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_response(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->status, BinaryStatus::Bye);
    EXPECT_TRUE(decoded->close_connection);
}

TEST(BinaryProtocolTest, IncompleteMessage) {
    Request req;
    req.command = Command::Get;
    req.key = "mykey";

    auto encoded = BinaryProtocol::encode_request(req);

    // Truncate the message
    std::vector<uint8_t> partial(encoded.begin(), encoded.begin() + 5);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(partial, consumed);

    EXPECT_FALSE(decoded.has_value());
}

TEST(BinaryProtocolTest, HasCompleteMessage) {
    Request req;
    req.command = Command::Ping;

    auto encoded = BinaryProtocol::encode_request(req);

    EXPECT_TRUE(BinaryProtocol::has_complete_message(encoded));

    std::vector<uint8_t> partial(encoded.begin(), encoded.begin() + 2);
    EXPECT_FALSE(BinaryProtocol::has_complete_message(partial));
}

TEST(BinaryProtocolTest, MultipleMessagesInBuffer) {
    Request req1;
    req1.command = Command::Ping;

    Request req2;
    req2.command = Command::Get;
    req2.key = "testkey";

    auto encoded1 = BinaryProtocol::encode_request(req1);
    auto encoded2 = BinaryProtocol::encode_request(req2);

    std::vector<uint8_t> combined;
    combined.insert(combined.end(), encoded1.begin(), encoded1.end());
    combined.insert(combined.end(), encoded2.begin(), encoded2.end());

    size_t consumed1 = 0;
    auto decoded1 = BinaryProtocol::decode_request(combined, consumed1);
    ASSERT_TRUE(decoded1.has_value());
    EXPECT_EQ(decoded1->command, Command::Ping);

    std::vector<uint8_t> remaining(combined.begin() + consumed1, combined.end());

    size_t consumed2 = 0;
    auto decoded2 = BinaryProtocol::decode_request(remaining, consumed2);
    ASSERT_TRUE(decoded2.has_value());
    EXPECT_EQ(decoded2->command, Command::Get);
    EXPECT_EQ(decoded2->key, "testkey");
}

TEST(BinaryProtocolTest, BinaryDataInValue) {
    Request req;
    req.command = Command::Put;
    req.key = "binkey";
    req.value = std::string("\x00\x01\x02\xFF\xFE", 5);

    auto encoded = BinaryProtocol::encode_request(req);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->value, req.value);
}

}  // namespace kvstore::net::test