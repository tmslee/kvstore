#include "kvstore/net/server/connection.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>

#include "kvstore/net/binary_protocol.hpp"
#include "kvstore/net/text_protocol.hpp"

namespace kvstore::net::server {

Connection::Connection(int fd, const ProtocolLimits& limits) : fd_(fd), limits_(limits) {
    read_buffer_.reserve(4096);
    write_buffer_.reserve(4096);
}

Connection::~Connection() = default;  // FdGuard handles fd cleanup

/*
Blocking (default):
recv(fd, buf, size, 0);  // Waits forever until data arrives
send(fd, buf, size, 0);  // Waits until all data is sent
accept(fd, ...);         // Waits until a client connects

Non-blocking (after set_nonblocking(fd)):
recv(fd, buf, size, 0);  // Returns immediately with:
                         //   - n > 0: got n bytes
                         //   - -1 + EAGAIN: no data available right now
                         //   - 0: connection closed
send(fd, buf, size, 0);  // Returns immediately with:
                         //   - n > 0: sent n bytes (maybe less than requested!)
                         //   - -1 + EAGAIN: socket buffer full, try later
accept(fd, ...);         // Returns immediately with:
                         //   - fd >= 0: new client
                         //   - -1 + EAGAIN: no pending connections
*/
bool Connection::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

IoResult Connection::do_read() {
    uint8_t buf[4096];
    ssize_t n = recv(fd_.get(), buf, sizeof(buf), 0);

    if (n > 0) {
        // check size limit before appending
        size_t bytes_read = static_cast<size_t>(n);
        if (read_buffer_.size() + bytes_read > limits_.max_message_size) {
            return IoResult::Error;
        }
        read_buffer_.insert(read_buffer_.end(), buf, buf + bytes_read);
        return IoResult::Ok;
    } else if (n == 0) {
        return IoResult::Closed;
    } else {
        // n < 0: check errno
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return IoResult::WouldBlock;
        }
        return IoResult::Error;
    }
}

IoResult Connection::do_write() {
    size_t pending = write_buffer_.size() - write_offset_;
    if (pending == 0) {
        return IoResult::Ok;
    }

    // send from offset position (skip already-sent data)
    ssize_t n = send(fd_.get(), write_buffer_.data() + write_offset_, pending, MSG_NOSIGNAL);

    if (n > 0) {
        write_offset_ += static_cast<size_t>(n);
        // compact buffer when consumed portion exceeds threshold
        if (write_offset_ > kCompactThreshold) {
            write_buffer_.erase(write_buffer_.begin(), write_buffer_.begin() + write_offset_);
            write_offset_ = 0;
        }
        return IoResult::Ok;
    } else if (n == 0) {
        // send() returning 0 with pending data is rare - treat as WouldBlock and retry
        return IoResult::WouldBlock;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return IoResult::WouldBlock;
        }
        return IoResult::Error;
    }
}

std::optional<Request> Connection::try_parse_request() {
    size_t available = read_buffer_.size() - read_offset_;
    if (available == 0) {
        return std::nullopt;
    }

    // auto detect protocol on first request
    // Binary protocol: first 4 bytes are length (big-endian uint32), so first byte is 0x00 for
    // messages < 16MB Text protocol: commands start with ASCII letters (GET, PUT, etc.), never 0x00
    if (!protocol_detected_) {
        uint8_t first_byte = read_buffer_[read_offset_];
        is_binary_ = (first_byte == 0x00);
        protocol_detected_ = true;
    }

    std::optional<Request> req;

    if (is_binary_) {
        // binary protocol: compact first since BinaryProtocol expects buffer to start at message
        if (read_offset_ > 0) {
            read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + read_offset_);
            read_offset_ = 0;
        }

        // need 4 byte length header first
        if (read_buffer_.size() < 4) {
            return std::nullopt;
        }

        // check if complete msg arrived
        if (!BinaryProtocol::has_complete_message(read_buffer_)) {
            return std::nullopt;
        }

        size_t consumed = 0;
        req = BinaryProtocol::decode_request(read_buffer_, consumed);
        read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + consumed);

    } else {
        // text protocol: check line length limit
        auto start = read_buffer_.begin() + read_offset_;
        auto it = std::find(start, read_buffer_.end(), '\n');

        // check if accumulated data exceeds line limit (even before newline)
        size_t line_length =
            (it == read_buffer_.end()) ? (read_buffer_.end() - start) : (it - start);
        if (line_length > limits_.max_line_size) {
            protocol_error_ = "line too long";
            return std::nullopt;
        }

        if (it == read_buffer_.end()) {
            return std::nullopt;
        }

        // extract line (excluding \n \r if present)
        std::string line(start, it);
        size_t consumed = (it - start) + 1;  // +1 for the newline
        read_offset_ += consumed;

        // compact buffer when consumed portion exceeds threshold
        if (read_offset_ > kCompactThreshold) {
            read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + read_offset_);
            read_offset_ = 0;
        }

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        req = TextProtocol::decode_request(line);
    }

    // validate key/value sizes
    if (req) {
        if (req->key.size() > limits_.max_key_size) {
            protocol_error_ = "key too large";
            return std::nullopt;
        }
        if (req->value.size() > limits_.max_value_size) {
            protocol_error_ = "value too large";
            return std::nullopt;
        }
    }

    return req;
}

void Connection::queue_response(const Response& response) {
    std::vector<uint8_t> data;

    if (is_binary_) {
        data = BinaryProtocol::encode_response(response);
    } else {
        std::string text = TextProtocol::encode_response(response);
        data.assign(text.begin(), text.end());
    }

    write_buffer_.insert(write_buffer_.end(), data.begin(), data.end());
}

bool Connection::has_pending_write() const {
    return write_buffer_.size() > write_offset_;
}

}  // namespace kvstore::net::server