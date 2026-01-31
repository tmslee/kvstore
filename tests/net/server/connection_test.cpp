#include <gtest/gtest.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

#include "kvstore/net/server/connection.hpp"

using namespace kvstore::net::server;

class ConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds_), 0);
        Connection::set_nonblocking(fds_[0]);
        Connection::set_nonblocking(fds_[1]);
    }  

    void TearDown() override {
        if(fds_[1] >= 0) close (fds_[1]);
    }

    int fds_[2] = {-1,-1};
};

TEST_F(ConnectionTest, CreateAndDestory) {}

TEST_F(ConnectionTest, SetNonblocking) {}

TEST_F(ConnectionTest, DoReadWouldBlock) {}

TEST_F(ConnectionTest, DoReadData) {}

TEST_F(ConnectionTest, DoReadClosed) {}

TEST_F(ConnectionTest, DoWriteEmpty) {}

TEST_F(ConnectionTest, HasPendingWrite) {}

TEST_F(ConnectionTest, QueueAndWriteResponse) {}

TEST_F(ConnectionTest, TryParseRequestEmpty) {}

TEST_F(ConnectionTest, TryParseTextRequest) {}

TEST_F(ConnectionTest, TryParsePartialRequest) {}
