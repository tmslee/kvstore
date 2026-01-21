#ifndef KVSTORE_NET_SERVER_HPP
#define KVSTORE_NET_SERVER_HPP

#include <cstdint>
#include <memory>
#include <string>

#include "kvstore/core/store.hpp"

namespace kvstore::net {

struct ServerOptions {
    std::string host = "127.0.0.1";  // local host
    uint16_t port = 6379;            // redis' default port. convention for k-v stores
};

class Server {
   public:
    Server(core::Store& store, const ServerOptions& options = {});
    ~Server();
    /*
        note on copy&moves:
        - server owns threads, socket fd, and reference to store. copying is nonsensical.
        - unclear ownership of core::Store& store_ after move. delete moves
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
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace kvstore::net

#endif