#include "kvstore/protocol.hpp"

#include <gtest/gtest.h>

namespace kvstore::test {

TEST(ProtocolTest, ParseSimpleCommand) {}

TEST(ProtocolTest, ParseCommandWithMultipleArgs) {}

TEST(ProtocolTest, ParseCommandCaseInsensitive) {}

TEST(ProtocolTest, ParseEmptyLine) {}

TEST(ProtocolTest, ParseWhitespaceOnly) {}

TEST(ProtocolTest, ParseTrimsWhitespace) {}

TEST(ProtocolTest, SerializeOk) {}

TEST(ProtocolTest, SerializeOkWithMessage) {}

TEST(ProtocolTest, SerializeError) {}

TEST(ProtocolTest, SerializeBye) {}

TEST(ProtocolTest, OkDoesNotCloseConnection) {}

TEST(ProtocolTest, NotFoundDoesNotCloseConnection) {}

TEST(ProtocolTest, ErrorDoesNotCloseConnection) {}

} //namespace kvstore::test