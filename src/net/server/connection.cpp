#include "kvstore/net/server/connection.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

#include "kvstore/net/binary_protocol.hpp"
#include "kvstore/net/text_protocol.hpp"

namespace kvstore::net::server {

Connection::Connection(int fd, const ProtocolLimits& limits) : fd_(fd), limits_(limits) {
    read_buffer_.reserve(4096);
    write_buffer_.reserve(4096);
}

Connection::~Connection() {
    if(fd_ >= 0) {
        close(fd_);
    }
}

bool Connection::set_noblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if(flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

IoResult Connection::do_read() {
    uint8_t buf[4096];
    ssize_t n = recv(fd_, buf, sizeof(buf), 0);

    if(n > 0) {
        //check size limit before appending
        if(read_buffer_.size() + n > limits_.max_message_size) {
            return IoResult::Error;
        }
        read_buffer_.insert(read_buffer_.end(), buf, buf+n);
        return IoResult::Ok;
    } else if (n == 0) {
        return IoResult::Closed;
    } else {
        // n < 0: check errno
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            return IoResult::WouldBlock;
        }
        return IoResult::Error;
    }
}

IoResult Connection::do_write() {
    if(write_buffer_.empty()) {
        return IoResult::Ok;
    }

    ssize_t n = send(fd_, write_buffer_.data(), write_buffer_.size(), MSG_NOSIGNAL);

    if(n > 0) {
        write_buffer_.erase(write_buffer_.begin(), write_buffer_.begin() + n);
        return IoResult::Ok;
    } else if (n == 0) {
        return IoResult::Closed;
    } else {
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            return IoResult::WouldBlock;
        }
        return IoResult::Error;
    }
}

std::optional<Request> Connection::try_parse_request() {
    if(read_buffer_.empty()) {
        return std::nullopt;
    }
    
    //auto detect protocol on first request
    if(!protocol_detected_) {
        uint8_t first_byte = read_buffer_[0];
        is_binary_ = (first_byte == 0x00 || first_byte > 127);
        protocol_detected_ = true;
    }

    if(is_binary_) {
        //binary protocol: need 4 byte length header first
        if(read_buffer_.size() < 4) {
            return std::nullopt;
        }
        
        //check if complete msg arrived
        if(!BinaryProtocol::has_complete_message(read_buffer_)) {
            return std:: nullopt;
        }

        size_t consumed = 0;
        auto req = BinaryProtocol::decode_request(read_buffer_, consumed);
        read_buffer_.erase(read_buffer_begin(), read_buffer_.begin() + consumed);
        return req;
    
    } else {
        // text protocol: look for newline
        auto it = std::find(read_buffer_.begin(), read_buffer_end(), '\n');
        if(it == read_buffer_.end()){
            return std::nullopt;
        }

        //extract line (excluding \n \r if present)
        std::string line(read_buffer_.begin(), it);
        read_buffer_.erase(read_buffer_.begin(), it+1);

        if(!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        return TextProtocol::decode_request(line);
    }

}

void Connection::queue_response(const Response& response) {
    std::vector<uint8_t> data;
    
    if(is_binary_) {
        data = BinaryProtocol::encode_response(response);
    } else {
        std::string text = TextProtocol::encode_response(response);
        data.assign(text.begin(), text.end());
    }

    write_buffer_.insert(write_buffer_.end(), data.begin(), data.end());
}

bool Connection::has_pending_write() const {
    return !write_buffer_.empty();
}

} //namespace kvstore::net::server