#include "kvstore/util/signal_handler.hpp"

#include <gtest/gtest.h>

#include <csignal>
#include <thread>

namespace kvstore::util::test {

class SignalHandlerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        SignalHandler::reset();
        SignalHandler::install();
    }
};

TEST_F(SignalHandlerTest, InitiallyNotShutdown) {
    EXPECT_FALSE(SignalHandler::should_shutdown());
}

TEST_F(SignalHandlerTest, RequestShutdown) {
    SignalHandler::request_shutdown();
    EXPECT_TRUE(SignalHandler::should_shutdown());
}

TEST_F(SignalHandlerTest, HandlesSIGINT) {
    std::thread waiter([] { SignalHandler::wait_for_shutdown(); });

    // wait for waiter thread to actually be waiting (deterministic, no fixed sleep)
    while (!SignalHandler::waiting()) {
        std::this_thread::yield();
    }
    EXPECT_FALSE(SignalHandler::should_shutdown());

    raise(SIGINT);

    waiter.join();
    EXPECT_TRUE(SignalHandler::should_shutdown());
}

TEST_F(SignalHandlerTest, HandlesSIGTERM) {
    SignalHandler::reset();

    std::thread waiter([] { SignalHandler::wait_for_shutdown(); });

    // wait for waiter thread to actually be waiting (deterministic, no fixed sleep)
    while (!SignalHandler::waiting()) {
        std::this_thread::yield();
    }
    EXPECT_FALSE(SignalHandler::should_shutdown());

    raise(SIGTERM);

    waiter.join();
    EXPECT_TRUE(SignalHandler::should_shutdown());
}

}  // namespace kvstore::util::test