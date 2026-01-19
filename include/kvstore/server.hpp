#ifndef KVSTORE_SERVER_HPP
#define KVSTORE_SERVER_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "kvstore/kvstore.hpp"

namespace kvstore {

struct ServerOptions {
    std::string host = "127.0.0.1";  // local host
    uint16_t port = 6379;            // redis' default port. convention for k-v stores
};

class Server {
   public:
    Server(KVStore& store, const ServerOptions& options = {});
    ~Server();

    /*
        note on copy&moves:
        - server owns threads, socket fd, and reference to store. copying is nonsensical.
        - unclear ownership of KVStore& store_ after move. delete moves
            - also threads capture this pointer in their lambdas. moving would lead those threads
       holding dangling pointers to old location
    */
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;

    void start();
    void stop();

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] uint16_t port() const noexcept;

   private:
    void accept_loop();
    void handle_client(int client_fd);
    std::string process_command(const std::string& line);

    KVStore& store_;
    ServerOptions options_;

    std::atomic<int> server_fd_ = -1;
    // server_fd_ needs to be atomic bc it gets written by main thread in stop()
    // while its read in accept_loop() by another thread.
    std::atomic<bool> running_ = false;

    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;
    std::mutex clients_mutex_;
};

}  // namespace kvstore

#endif