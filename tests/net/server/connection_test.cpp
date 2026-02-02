#include "kvstore/net/server/connection.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

using namespace kvstore::net::server;
using namespace kvstore::net;

class ConnectionTest : public ::testing::Test {
   protected:
    void SetUp() override {
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds_), 0);
        Connection::set_nonblocking(fds_[0]);
        Connection::set_nonblocking(fds_[1]);
    }

    void TearDown() override {
        if (fds_[1] >= 0)
            close(fds_[1]);
    }

    int fds_[2] = {-1, -1};
};

TEST_F(ConnectionTest, CreateAndDestory) {
    int fd = fds_[0];
    Connection conn(fd);
    fds_[0] = -1;  // connection takes ownership
    EXPECT_EQ(conn.fd(), fd);
}

TEST_F(ConnectionTest, SetNonblocking) {
    int flags = fcntl(fds_[0], F_GETFL, 0);
    EXPECT_TRUE(flags & O_NONBLOCK);
}

TEST_F(ConnectionTest, DoReadWouldBlock) {
    Connection conn(fds_[0]);
    fds_[0] = -1;

    IoResult result = conn.do_read();
    EXPECT_EQ(result, IoResult::WouldBlock);
}

TEST_F(ConnectionTest, DoReadData) {
    Connection conn(fds_[0]);
    fds_[0] = -1;

    const char* msg = "hello";
    write(fds_[1], msg, 5);

    IoResult result = conn.do_read();
    EXPECT_EQ(result, IoResult::Ok);
}

TEST_F(ConnectionTest, DoReadClosed) {
    Connection conn(fds_[0]);
    fds_[0] = -1;

    close(fds_[1]);
    fds_[1] = -1;

    IoResult result = conn.do_read();
    EXPECT_EQ(result, IoResult::Closed);
}

TEST_F(ConnectionTest, DoWriteEmpty) {
    Connection conn(fds_[0]);
    fds_[0] = -1;

    IoResult result = conn.do_write();
    EXPECT_EQ(result, IoResult::Ok);
}

TEST_F(ConnectionTest, HasPendingWrite) {
    Connection conn(fds_[0]);
    fds_[0] = -1;

    EXPECT_FALSE(conn.has_pending_write());

    conn.queue_response(Response::ok("test"));
    EXPECT_TRUE(conn.has_pending_write());
}

TEST_F(ConnectionTest, QueueAndWriteResponse) {
    Connection conn(fds_[0]);
    fds_[0] = -1;

    conn.queue_response(Response::ok("hello"));
    EXPECT_TRUE(conn.has_pending_write());

    IoResult result = conn.do_write();
    EXPECT_EQ(result, IoResult::Ok);

    char buf[256];
    ssize_t n = read(fds_[1], buf, sizeof(buf));
    EXPECT_GT(n, 0);
}

TEST_F(ConnectionTest, TryParseRequestEmpty) {
    Connection conn(fds_[0]);
    fds_[0] = -1;

    auto req = conn.try_parse_request();
    EXPECT_FALSE(req.has_value());
}

TEST_F(ConnectionTest, TryParseTextRequest) {
    Connection conn(fds_[0]);
    fds_[0] = -1;

    const char* msg = "GET mykey\r\n";
    write(fds_[1], msg, strlen(msg));

    conn.do_read();
    auto req = conn.try_parse_request();

    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->command, Command::Get);
    EXPECT_EQ(req->key, "mykey");
}

TEST_F(ConnectionTest, TryParsePartialRequest) {
    Connection conn(fds_[0]);
    fds_[0] = -1;

    const char* msg = "GET mykey";
    write(fds_[1], msg, strlen(msg));

    conn.do_read();
    auto req = conn.try_parse_request();
    EXPECT_FALSE(req.has_value());

    write(fds_[1], "\r\n", 2);
    conn.do_read();
    req = conn.try_parse_request();

    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->command, Command::Get);
}

TEST_F(ConnectionTest, WriteMultipleResponses) {
    Connection conn(fds_[0]);
    fds_[0] = -1;

    conn.queue_response(Response::ok("val1"));
    conn.queue_response(Response::ok("val2"));

    while (conn.has_pending_write()) {
        conn.do_write();
    }

    char buf[512];
    ssize_t n = read(fds_[1], buf, sizeof(buf));
    EXPECT_GT(n, 0);
}