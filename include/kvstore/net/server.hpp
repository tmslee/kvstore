#ifndef KVSTORE_NET_SERVER_HPP
#define KVSTORE_NET_SERVER_HPP

#include <cstdint>
#include <memory>
#include <string>

#include "kvstore/core/istore.hpp"

namespace kvstore::net {

struct ServerOptions {
    std::string host = "127.0.0.1";  // local host
    uint16_t port = 6379;            // redis' default port. convention for k-v stores
    std::size_t max_connections = 1000;
    int client_timeout_seconds = 300;  // 5 minutes
};

class Server {
   public:
    Server(core::IStore& store, const ServerOptions& options = {});
    ~Server();
    /*
        note on copy&moves:
        - server owns Impl which has threads & mutex and socket fds under the hood
        - copying is not safe
        - move would normally not safe either but since we did PIMPL, move is trivial and allowed
    */
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) noexcept;
    Server& operator=(Server&&) noexcept;

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