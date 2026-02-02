#ifndef KVSTORE_NET_SERVER_CONNECTION_HPP
#define KVSTORE_NET_SERVER_CONNECTION_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <optional>

#include "kvstore/net/server/protocol_handler.hpp"
#include "kvstore/net/types.hpp"

namespace kvstore::net::server {

// Result of a non-blocking read/write operation
enum class IoResult {
    Ok,          // operation completed successfully
    WouldBlock,  // need to wait for more data/ socket not ready
    Error,       // connection error, should close
    Closed       // peer closed connection
};

// represent a single client connection with its state and buffers
class Connection {
   public:
    explicit Connection(int fd, const ProtocolLimits& limits = {});
    ~Connection();

    // non copyable, non moveable (owns fd)
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;

    // try to read data from socket into read buffer (non-blocking)
    IoResult do_read();

    // try to write data from write buffer to scoket (non-blocking)
    IoResult do_write();

    // try to parse a complete request from the read buffer
    // returns nullopt if not enough data yet
    [[nodiscard]] std::optional<Request> try_parse_request();

    // queue a response to be written
    void queue_response(const Response& response);

    // check if theres data waiting to be written
    [[nodiscard]] bool has_pending_write() const;

    // get file descriptor
    [[nodiscard]] int fd() const noexcept {
        return fd_;
    }

    // set socket to non-blocking mode
    static bool set_nonblocking(int fd);

   private:
    int fd_;
    ProtocolLimits limits_;
    bool is_binary_{false};
    bool protocol_detected_{false};

    // buffers for partial reads/writes
    std::vector<uint8_t> read_buffer_;
    std::vector<uint8_t> write_buffer_;
};

}  // namespace kvstore::net::server

#endif