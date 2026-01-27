#include "kvstore/net/binary_protocol.hpp"

#include <gtest/gtest.h>

namespace kvstore::net::test {

TEST(BinaryProtocolTest, EncodeDecodeRequestGet) {
    Request req{Command::Get, "mykey", "", 0};

    auto encoded = BinaryProtocol::encode_request(req);
    EXPECT_TRUE(BinaryProtocol::has_complete_message(encoded));

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->command, Command::Get);
    EXPECT_EQ(decoded->key, "mykey");
    EXPECT_EQ(consumed, encoded.size());
}

TEST(BinaryProtocolTest, EncodeDecodeRequestPut) {
    Request req{Command::Put, "mykey", "myvalue", 0};

    auto encoded = BinaryProtocol::encode_request(req);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->command, Command::Put);
    EXPECT_EQ(decoded->key, "mykey");
    EXPECT_EQ(decoded->value, "myvalue");
}

TEST(BinaryProtocolTest, EncodeDecodeRequestPutEx) {
    Request req{Command::PutEx, "mykey", "myvalue", 60000};

    auto encoded = BinaryProtocol::encode_request(req);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->command, Command::PutEx);
    EXPECT_EQ(decoded->key, "mykey");
    EXPECT_EQ(decoded->value, "myvalue");
    EXPECT_EQ(decoded->ttl_ms, 60000);
}

TEST(BinaryProtocolTest, EncodeDecodeRequestDel) {
    Request req{Command::Del, "mykey", "", 0};

    auto encoded = BinaryProtocol::encode_request(req);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->command, Command::Del);
    EXPECT_EQ(decoded->key, "mykey");
}

TEST(BinaryProtocolTest, EncodeDecodeRequestExists) {
    Request req{Command::Exists, "mykey", "", 0};

    auto encoded = BinaryProtocol::encode_request(req);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->command, Command::Exists);
    EXPECT_EQ(decoded->key, "mykey");
}

TEST(BinaryProtocolTest, EncodeDecodeRequestPing) {
    Request req{Command::Ping, "", "", 0};

    auto encoded = BinaryProtocol::encode_request(req);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->command, Command::Ping);
}

TEST(BinaryProtocolTest, EncodeDecodeRequestSize) {
    Request req{Command::Size, "", "", 0};

    auto encoded = BinaryProtocol::encode_request(req);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->command, Command::Size);
}

TEST(BinaryProtocolTest, EncodeDecodeRequestClear) {
    Request req{Command::Clear, "", "", 0};

    auto encoded = BinaryProtocol::encode_request(req);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->command, Command::Clear);
}

TEST(BinaryProtocolTest, EncodeDecodeRequestQuit) {
    Request req{Command::Quit, "", "", 0};

    auto encoded = BinaryProtocol::encode_request(req);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->command, Command::Quit);
}

TEST(BinaryProtocolTest, EncodeDecodeResponseOk) {
    Response resp{Status::Ok, "PONG", false};

    auto encoded = BinaryProtocol::encode_response(resp);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_response(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->status, Status::Ok);
    EXPECT_EQ(decoded->data, "PONG");
    EXPECT_FALSE(decoded->close_connection);
}

TEST(BinaryProtocolTest, EncodeDecodeResponseOkEmpty) {
    Response resp{Status::Ok, "", false};

    auto encoded = BinaryProtocol::encode_response(resp);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_response(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->status, Status::Ok);
    EXPECT_TRUE(decoded->data.empty());
}

TEST(BinaryProtocolTest, EncodeDecodeResponseNotFound) {
    Response resp{Status::NotFound, "", false};

    auto encoded = BinaryProtocol::encode_response(resp);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_response(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->status, Status::NotFound);
}

TEST(BinaryProtocolTest, EncodeDecodeResponseError) {
    Response resp{Status::Error, "something went wrong", false};

    auto encoded = BinaryProtocol::encode_response(resp);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_response(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->status, Status::Error);
    EXPECT_EQ(decoded->data, "something went wrong");
}

TEST(BinaryProtocolTest, EncodeDecodeResponseBye) {
    Response resp{Status::Bye, "", true};

    auto encoded = BinaryProtocol::encode_response(resp);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_response(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->status, Status::Bye);
    EXPECT_TRUE(decoded->close_connection);
}

TEST(BinaryProtocolTest, IncompleteMessage) {
    Request req{Command::Get, "mykey", "", 0};
    auto encoded = BinaryProtocol::encode_request(req);

    // Truncate
    std::vector<uint8_t> partial(encoded.begin(), encoded.begin() + 5);

    EXPECT_FALSE(BinaryProtocol::has_complete_message(partial));

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(partial, consumed);
    EXPECT_FALSE(decoded.has_value());
}

TEST(BinaryProtocolTest, TooShortForHeader) {
    std::vector<uint8_t> tiny = {0x00, 0x00};

    EXPECT_FALSE(BinaryProtocol::has_complete_message(tiny));
    EXPECT_EQ(BinaryProtocol::peek_message_length(tiny), 0);
}

TEST(BinaryProtocolTest, MultipleMessages) {
    Request req1{Command::Ping, "", "", 0};
    Request req2{Command::Get, "testkey", "", 0};

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
    std::string binary_value("\x00\x01\x02\xFF\xFE", 5);
    Request req{Command::Put, "binkey", binary_value, 0};

    auto encoded = BinaryProtocol::encode_request(req);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->value, binary_value);
    EXPECT_EQ(decoded->value.size(), 5);
}

TEST(BinaryProtocolTest, LargeValue) {
    std::string large_value(100000, 'x');
    Request req{Command::Put, "largekey", large_value, 0};

    auto encoded = BinaryProtocol::encode_request(req);

    size_t consumed = 0;
    auto decoded = BinaryProtocol::decode_request(encoded, consumed);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->value.size(), 100000);
    EXPECT_EQ(decoded->value, large_value);
}

TEST(BinaryProtocolTest, PeekMessageLength) {
    Request req{Command::Ping, "", "", 0};
    auto encoded = BinaryProtocol::encode_request(req);

    uint32_t len = BinaryProtocol::peek_message_length(encoded);
    EXPECT_EQ(len, 1);  // Just command byte
}

}  // namespace kvstore::net::test