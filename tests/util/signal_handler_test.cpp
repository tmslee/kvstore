#include "kvstore/util/signal_handler.hpp"

#include <gtest/gtest.h>

#include <thread>

namespace kvstore::util::test {

TEST(SignalHandlerTest, InitiallyNotShutdown) {
    //note: cant fully reset static state between tests
    //this test assumes it runs first or state is clean
    EXPECT_FALSE(SignalHandler::should_shutdown());
}

TEST(SignalHandlerTest, RequestShutdown) {
    SignalHandler::request_shutdown();
    EXPECT_TRUE(SignalHandler::should_shutdown());
}

TEST(SignalHandlerTest, WaitForShutdownReuturnsWhenRequested) {
    //request is already set from previous test
    //wait_for_shutdown should return immediately
    SignalHandler::wait_for_shutdown();
    EXPECT_TRUE(SignalHandler::should_shutdown());
}

} //namespace kvstore::util::test