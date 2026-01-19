#include "kvstore/kvstore.hpp"
#include "kvstore/server.hpp"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys.socket.h>
#include <uninstd.h>

#include <memory>
#include <string>
#include <thread>

namespace kvstore::test {
class ServerTest : public ::testing::Test {
protected:
    void Setup() override {

    }
    void TearDown() override {

    }
    
    std::string send_command(const std::string& cmd) {

    }
};

TEST_F(ServerTest, Ping){}

TEST_F(ServerTest, PutAndGet){}

TEST_F(ServerTest, GetMissing){}

TEST_F(ServerTest, Delete){}

TEST_F(ServerTest, Exists){}

TEST_F(ServerTest, Size){}

TEST_F(ServerTest, Clear){}

TEST_F(ServerTest, UnkownCommand){}

} //namespace kvstore::test


