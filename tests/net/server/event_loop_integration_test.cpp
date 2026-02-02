#include <arpa/inet.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <thread>
#include <unordered_map>

#include "kvstore/core/store.hpp"
#include "kvstore/net/server/connection.hpp"
#include "kvstore/net/server/event_loop.hpp"
#include "kvstore/net/types.hpp"

using namespace kvstore::net::server;
using namespace kvstore::net;

class EventLoopIntegrationTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // create server socket
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(server_fd_, 0);

        // allow reuse of address immediately after close
        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // bind to port 0 = "OS picks random available port"
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = 0;  // let OS choose port

        // bind to port & start listening (queue up to 10 connections)
        ASSERT_EQ(bind(server_fd_, (sockaddr*)&addr, sizeof(addr)), 0);
        ASSERT_EQ(listen(server_fd_, 10), 0);

        // retrieve actual port the OS assigned
        socklen_t len = sizeof(addr);
        getsockname(server_fd_, (sockaddr*)&addr, &len);
        port_ = ntohs(addr.sin_port);

        // make non-blocking so accpet() returns immediately if no connection
        Connection::set_nonblocking(server_fd_);
    }

    void TearDown() override {
        if (server_fd_ >= 0)
            close(server_fd_);
    }

    int connect_client() {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        connect(fd, (sockaddr*)&addr, sizeof(addr));
        return fd;
    }

    int server_fd_ = -1;
    uint16_t port_ = 0;
};

TEST_F(EventLoopIntegrationTest, AcceptConnection) {
    // verify that eventlopp can detect incoming connections with epoll
    EventLoop loop;
    bool accepted = false;

    // add server to epoll & watch for EPOLLIN (readable)
    // callback is: accept connection, close client_fd, stop the loop.
    loop.add(server_fd_, EPOLLIN, [&](int fd, uint32_t) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(fd, (sockaddr*)&client_addr, &len);
        EXPECT_GE(client_fd, 0);
        close(client_fd);
        accepted = true;
        loop.stop();
    });

    // connect from another thread
    std::thread client([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        int fd = connect_client();
        close(fd);
    });

    // loop.run() start, epoll_wait() blocks. on client connect, epoll wait returns, fire callback.
    loop.run();
    client.join();

    EXPECT_TRUE(accepted);
}

TEST_F(EventLoopIntegrationTest, EchoServer) {
    EventLoop loop;
    std::unordered_map<int, std::unique_ptr<Connection>> connections;

    // accept handler
    /*
        flow:
        - when server_fd_ is readable, accept new client, make it non blocking.
        - create a connection obj to track its state
        - add client_fd to epoll (watch for EPOLLIN)
        - when client_fd is readable:
            - read into buffer
            - if closed/error -> remove from epoll & cleanup
            - if request is complate:
                - create response & put in write buffer
                - modify epoll to also watch for EPOLLOUT
        - when client_fd is wriateable (EPOLLOUT)
            - flush write buffer to socket
            - if nothing left to write, stop watching EPOLLOUT
    */
    loop.add(server_fd_, EPOLLIN, [&](int fd, uint32_t) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(fd, (sockaddr*)&client_addr, &len);
        if (client_fd < 0)
            return;

        Connection::set_nonblocking(client_fd);
        connections[client_fd] = std::make_unique<Connection>(client_fd);

        loop.add(client_fd, EPOLLIN, [&, client_fd](int, uint32_t ev) {
            auto it = connections.find(client_fd);
            if (it == connections.end())
                return;  // already removed
            auto& conn = it->second;

            if (ev & EPOLLIN) {
                auto result = conn->do_read();
                if (result == IoResult::Closed || result == IoResult::Error) {
                    loop.remove(client_fd);
                    connections.erase(client_fd);
                    return;
                }

                // Try to parse request
                if (auto req = conn->try_parse_request()) {
                    // echo back
                    Response resp = Response::ok(req->key);
                    conn->queue_response(resp);
                    loop.modify(client_fd, EPOLLIN | EPOLLOUT);
                }
            }

            if (ev & EPOLLOUT) {
                conn->do_write();
                if (!conn->has_pending_write()) {
                    loop.modify(client_fd, EPOLLIN);
                }
            }
        });
    });

    // client thread
    std::thread client([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        int fd = connect_client();

        const char* msg = "GET hello\r\n";
        write(fd, msg, strlen(msg));

        char buf[256];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
        } else {
            buf[0] = '\0';
        }

        EXPECT_TRUE(strstr(buf, "hello") != nullptr);

        close(fd);
        loop.stop();
    });

    loop.run();
    client.join();
}

TEST_F(EventLoopIntegrationTest, MultipleClients) {
    EventLoop loop;
    std::unordered_map<int, std::unique_ptr<Connection>> connections;
    int clients_served = 0;

    loop.add(server_fd_, EPOLLIN, [&](int fd, uint32_t) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(fd, (sockaddr*)&client_addr, &len);
        if (client_fd < 0)
            return;

        Connection::set_nonblocking(client_fd);
        connections[client_fd] = std::make_unique<Connection>(client_fd);

        loop.add(client_fd, EPOLLIN, [&, client_fd](int, uint32_t ev) {
            auto it = connections.find(client_fd);
            if (it == connections.end())
                return;  // already removed
            auto& conn = it->second;

            if (ev & EPOLLIN) {
                auto result = conn->do_read();
                if (result == IoResult::Closed || result == IoResult::Error) {
                    loop.remove(client_fd);
                    connections.erase(client_fd);
                    clients_served++;
                    if (clients_served >= 3) {
                        loop.stop();
                    }
                    return;
                }

                if (auto req = conn->try_parse_request()) {
                    conn->queue_response(Response::ok("pong"));
                    loop.modify(client_fd, EPOLLIN | EPOLLOUT);
                }
            }

            if (ev & EPOLLOUT) {
                conn->do_write();
                if (!conn->has_pending_write()) {
                    loop.modify(client_fd, EPOLLIN);
                }
            }
        });
    });

    // launch 3 clients
    std::vector<std::thread> clients;
    for (int i = 0; i < 3; ++i) {
        clients.emplace_back([this, i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50 + i * 10));
            int fd = connect_client();
            write(fd, "PING\r\n", 6);
            char buf[64];
            read(fd, buf, sizeof(buf));
            close(fd);
        });
    }

    loop.run();
    for (auto& t : clients)
        t.join();

    EXPECT_EQ(clients_served, 3);
}
