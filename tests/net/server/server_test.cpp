#include "kvstore/net/server/server.hpp"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <thread>

#include "kvstore/core/disk_store.hpp"
#include "kvstore/core/store.hpp"

namespace kvstore::net::test {
class ServerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        store_ = std::make_unique<core::Store>();
        server::ServerOptions opts;
        opts.port =
            16379;  // test port; avoid conflicts with real redis. ports above 1024 dont need root
        server_ = std::make_unique<server::Server>(*store_, opts);
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
        }
    }

    std::string send_command(const std::string& cmd) {
        // test commands are small and responses are small.
        // in practice, send & receive will both fit in one TCP packet. one call respectively will
        // be sufficient for sending & receiving. we don't need a loop
        /*
            for client implementation:

            size_t total_sent = 0;
            while(total_send < msg.size()) {
                ... handle partial writes
            }

            // read until newline
            std::string response:
            char c;
            while(recv(sock, &c, 1, 0) == 1 && c != '\n) {
                response += c;
            }
        */
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            return "ERROR socket creation failed";
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(16379);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(sock);
            return "ERROR connection failed";
        }

        std::string msg = cmd + "\n";
        send(sock, msg.c_str(), msg.size(), 0);

        char buffer[1024];
        ssize_t n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        close(sock);

        if (n <= 0) {
            return "ERROR no response";
        }

        buffer[n] = '\0';
        std::string response(buffer);
        if (!response.empty() && response.back() == '\n') {
            response.pop_back();
        }
        return response;
    }

    // we use std::unique_ptr so we can control construction timing in Setup() - if plain members,
    // object would be constructed during fixture construction, before Setup(). also allows
    // tearDown() to destroy early if needed
    std::unique_ptr<core::Store> store_;
    std::unique_ptr<server::Server> server_;
};

TEST_F(ServerTest, Ping) {
    server_->start();
    EXPECT_EQ(send_command("PING"), "OK PONG");
}

TEST_F(ServerTest, PutAndGet) {
    server_->start();

    EXPECT_EQ(send_command("PUT foo bar"), "OK");
    EXPECT_EQ(send_command("GET foo"), "OK bar");
}

TEST_F(ServerTest, GetMissing) {
    server_->start();

    EXPECT_EQ(send_command("GET nonexistent"), "NOT_FOUND");
}

TEST_F(ServerTest, Delete) {
    server_->start();

    EXPECT_EQ(send_command("PUT foo bar"), "OK");
    EXPECT_EQ(send_command("DEL foo"), "OK");
    EXPECT_EQ(send_command("GET foo"), "NOT_FOUND");
}

TEST_F(ServerTest, Exists) {
    server_->start();

    EXPECT_EQ(send_command("EXISTS foo"), "OK 0");
    EXPECT_EQ(send_command("PUT foo bar"), "OK");
    EXPECT_EQ(send_command("EXISTS foo"), "OK 1");
}

TEST_F(ServerTest, Size) {
    server_->start();

    EXPECT_EQ(send_command("SIZE"), "OK 0");
    EXPECT_EQ(send_command("PUT foo bar"), "OK");
    EXPECT_EQ(send_command("SIZE"), "OK 1");
    EXPECT_EQ(send_command("PUT baz qux"), "OK");
    EXPECT_EQ(send_command("SIZE"), "OK 2");
}

TEST_F(ServerTest, Clear) {
    server_->start();

    EXPECT_EQ(send_command("PUT foo bar"), "OK");
    EXPECT_EQ(send_command("PUT baz qux"), "OK");
    EXPECT_EQ(send_command("CLEAR"), "OK");
    EXPECT_EQ(send_command("SIZE"), "OK 0");
}

TEST_F(ServerTest, UnkownCommand) {
    server_->start();

    std::string response = send_command("INVALID");
    EXPECT_TRUE(response.find("ERROR") != std::string::npos);
}

TEST_F(ServerTest, PutEx) {
    server_->start();
    EXPECT_EQ(send_command("PUTEX foo 60000 bar"), "OK");
    EXPECT_EQ(send_command("GET foo"), "OK bar");
}

TEST_F(ServerTest, PutExWithSpaces) {
    server_->start();
    EXPECT_EQ(send_command("PUTEX foo 60000 hello world"), "OK");
    EXPECT_EQ(send_command("GET foo"), "OK hello world");
}

TEST_F(ServerTest, PutExInvalidTTL) {
    server_->start();
    std::string response = send_command("PUTEX foo notanumber bar");
    EXPECT_TRUE(response.find("ERROR") != std::string::npos);
}

TEST_F(ServerTest, SetEx) {
    server_->start();
    EXPECT_EQ(send_command("SETEX foo 60000 bar"), "OK");
    EXPECT_EQ(send_command("GET foo"), "OK bar");
}

// DiskStore tests

class ServerDiskStoreTest : public ::testing::Test {
   protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "server_disk_store_test";
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);

        core::DiskStoreOptions store_opts;
        store_opts.data_dir = test_dir_;
        store_ = std::make_unique<core::DiskStore>(store_opts);

        server::ServerOptions server_opts;
        server_opts.port = 16382;
        server_ = std::make_unique<server::Server>(*store_, server_opts);
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
        }
        store_.reset();
        std::filesystem::remove_all(test_dir_);
    }

    std::string send_command(const std::string& cmd) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            return "ERROR socket creation failed";
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(16382);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(sock);
            return "ERROR connection failed";
        }

        std::string msg = cmd + "\n";
        send(sock, msg.c_str(), msg.size(), 0);

        char buffer[1024];
        ssize_t n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        close(sock);

        if (n <= 0) {
            return "ERROR no response";
        }

        buffer[n] = '\0';
        std::string response(buffer);
        if (!response.empty() && response.back() == '\n') {
            response.pop_back();
        }
        return response;
    }

    std::filesystem::path test_dir_;
    std::unique_ptr<core::DiskStore> store_;
    std::unique_ptr<server::Server> server_;
};

TEST_F(ServerDiskStoreTest, Ping) {
    server_->start();
    EXPECT_EQ(send_command("PING"), "OK PONG");
}

TEST_F(ServerDiskStoreTest, PutAndGet) {
    server_->start();
    EXPECT_EQ(send_command("PUT foo bar"), "OK");
    EXPECT_EQ(send_command("GET foo"), "OK bar");
}

TEST_F(ServerDiskStoreTest, GetMissing) {
    server_->start();
    EXPECT_EQ(send_command("GET nonexistent"), "NOT_FOUND");
}

TEST_F(ServerDiskStoreTest, Delete) {
    server_->start();
    EXPECT_EQ(send_command("PUT foo bar"), "OK");
    EXPECT_EQ(send_command("DEL foo"), "OK");
    EXPECT_EQ(send_command("GET foo"), "NOT_FOUND");
}

TEST_F(ServerDiskStoreTest, Exists) {
    server_->start();
    EXPECT_EQ(send_command("EXISTS foo"), "OK 0");
    EXPECT_EQ(send_command("PUT foo bar"), "OK");
    EXPECT_EQ(send_command("EXISTS foo"), "OK 1");
}

TEST_F(ServerDiskStoreTest, Size) {
    server_->start();
    EXPECT_EQ(send_command("SIZE"), "OK 0");
    EXPECT_EQ(send_command("PUT foo bar"), "OK");
    EXPECT_EQ(send_command("SIZE"), "OK 1");
    EXPECT_EQ(send_command("PUT baz qux"), "OK");
    EXPECT_EQ(send_command("SIZE"), "OK 2");
}

TEST_F(ServerDiskStoreTest, Clear) {
    server_->start();
    EXPECT_EQ(send_command("PUT foo bar"), "OK");
    EXPECT_EQ(send_command("PUT baz qux"), "OK");
    EXPECT_EQ(send_command("CLEAR"), "OK");
    EXPECT_EQ(send_command("SIZE"), "OK 0");
}

TEST_F(ServerDiskStoreTest, PutEx) {
    server_->start();
    EXPECT_EQ(send_command("PUTEX foo 60000 bar"), "OK");
    EXPECT_EQ(send_command("GET foo"), "OK bar");
}

TEST_F(ServerDiskStoreTest, PutExWithSpaces) {
    server_->start();
    EXPECT_EQ(send_command("PUTEX foo 60000 hello world"), "OK");
    EXPECT_EQ(send_command("GET foo"), "OK hello world");
}

}  // namespace kvstore::net::test
