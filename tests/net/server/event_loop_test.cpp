#include "kvstore/net/server/event_loop.hpp"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <thread>

using namespace kvstore::net::server;

class EventLoopTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // create a socketpair for testing - gives us 2 connected fds
        // anything written to one can be read from other
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds_), 0);
    }

    void TearDown() override {
        if (fds_[0] >= 0)
            close(fds_[0]);
        if (fds_[1] >= 0)
            close(fds_[1]);
    }
    int fds_[2] = {-1, -1};
};

TEST_F(EventLoopTest, CreateAndDestory) {
    EventLoop loop;
    // should not throw
}

TEST_F(EventLoopTest, AddAndRemove) {
    EventLoop loop;
    bool called = false;

    loop.add(fds_[0], kEventRead, [&](int, uint32_t) { called = true; });

    loop.remove(fds_[0]);
    // should not throw
}

TEST_F(EventLoopTest, PollWithNoEvents) {
    EventLoop loop;
    loop.add(fds_[0], kEventRead, [](int, uint32_t) {});

    // poll with 0 timeout - should return immedaitely with no events
    int n = loop.poll(0);
    EXPECT_EQ(n, 0);
}

TEST_F(EventLoopTest, PollDetectsReadable) {
    EventLoop loop;
    bool called = false;
    int received_fd = -1;

    loop.add(fds_[0], kEventRead, [&](int fd, uint32_t events) {
        called = true;
        received_fd = fd;
        EXPECT_TRUE(events & kEventRead);
    });

    // write to fds_[1] makes fds_[0] readable
    const char* msg = "hello";
    (void)write(fds_[1], msg, 5);

    int n = loop.poll(100);
    EXPECT_EQ(n, 1);
    EXPECT_TRUE(called);
    EXPECT_EQ(received_fd, fds_[0]);
}

TEST_F(EventLoopTest, PollDetectsWritable) {
    EventLoop loop;
    bool called = false;

    loop.add(fds_[0], kEventWrite, [&](int, uint32_t events) {
        called = true;
        EXPECT_TRUE(events & kEventWrite);
    });

    // sokcet should be immediately writable
    int n = loop.poll(100);
    EXPECT_EQ(n, 1);
    EXPECT_TRUE(called);
}

TEST_F(EventLoopTest, ModifyEvents) {
    EventLoop loop;
    int call_count = 0;

    // start with kEventRead only
    loop.add(fds_[0], kEventRead, [&](int, uint32_t) { call_count++; });

    // no data to read. should not trigger
    loop.poll(0);
    EXPECT_EQ(call_count, 0);

    // modify to watch for kEventWrite
    loop.modify(fds_[0], kEventWrite);

    // should now trigger (socket writable)
    loop.poll(100);
    EXPECT_EQ(call_count, 1);
}

TEST_F(EventLoopTest, MultipleFileDescriptors) {
    // create another scoketpair
    int fds2[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds2), 0);

    EventLoop loop;
    int count1 = 0, count2 = 0;

    loop.add(fds_[0], kEventRead, [&](int, uint32_t) { count1++; });
    loop.add(fds2[0], kEventRead, [&](int, uint32_t) { count2++; });

    //  write to both
    (void)write(fds_[1], "a", 1);
    (void)write(fds2[1], "b", 1);

    // should get both events
    int n = loop.poll(100);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(count1, 1);
    EXPECT_EQ(count2, 1);

    close(fds2[0]);
    close(fds2[1]);
}

TEST_F(EventLoopTest, RunAndStop) {
    EventLoop loop;

    loop.add(fds_[0], kEventRead, [&](int, uint32_t) { loop.stop(); });

    // start loop in another thread
    std::thread t([&]() { loop.run(); });

    // give loop time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // write to trigger callback which calls stop()
    (void)write(fds_[1], "x", 1);

    // thread should exit
    t.join();

    EXPECT_FALSE(loop.running());
}

TEST_F(EventLoopTest, StopFromAnotherThread) {
    EventLoop loop;

    loop.add(fds_[0], kEventRead, [](int, uint32_t) {});

    std::thread t([&]() { loop.run(); });

    // give loop time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // stop from main thread
    loop.stop();

    t.join();
    EXPECT_FALSE(loop.running());
}

TEST_F(EventLoopTest, CallbackException) {
    EventLoop loop;

    loop.add(fds_[0], kEventRead,
             [](int, uint32_t) { throw std::runtime_error("test exception"); });

    (void)write(fds_[1], "x", 1);

    // should not crash - exception caught and logged
    int n = loop.poll(100);
    EXPECT_EQ(n, 1);
}
