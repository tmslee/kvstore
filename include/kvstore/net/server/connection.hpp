#ifndef KVSTORE_NET_SERVER_CONNECTION_HPP
#define KVSTORE_NET_SERVER_CONNECTION_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "kvstore/net/server/protocol_handler.hpp"
#include "kvstore/net/types.hpp"
#include "kvstore/util/fd_guard.hpp"

namespace kvstore::net::server {

// Result of a non-blocking read/write operation
enum class IoResult {
    Ok,          // operation completed successfully
    WouldBlock,  // need to wait for more data/ socket not ready
    Error,       // connection error, should close
    Closed       // peer closed connection
};

// Represents a single client connection with its state and buffers.
//
// Thread Safety: Connection is designed for single-threaded use within an event loop.
// All operations (do_read, do_write, try_parse_request, queue_response) must be called
// from the same thread. The server's event loop guarantees this - all connection handling
// happens in a single thread, so no synchronization is needed. This avoids the overhead
// of locking and the complexity of concurrent buffer access.
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
        return fd_.get();
    }

    // check if request was rejected due to protocol limits (key/value/line size)
    [[nodiscard]] bool has_protocol_error() const noexcept {
        return !protocol_error_.empty();
    }

    // get the protocol error message (empty if no error)
    [[nodiscard]] const std::string& protocol_error() const noexcept {
        return protocol_error_;
    }

    // set socket to non-blocking mode
    static bool set_nonblocking(int fd);

   private:
    util::FdGuard fd_;
    ProtocolLimits limits_;
    bool is_binary_{false};
    bool protocol_detected_{false};

    // buffers for partial reads/writes
    std::vector<uint8_t> read_buffer_;
    std::vector<uint8_t> write_buffer_;

    size_t read_offset_{0};
    size_t write_offset_{0};
    static constexpr size_t kCompactThreshold = 4096;

    std::string protocol_error_;  // set when request parsing fails due to limit violations
};

}  // namespace kvstore::net::server

#endif