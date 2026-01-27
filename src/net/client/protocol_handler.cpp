#include "kvstore/net/client/protocol_handler.hpp"

#include <sys/socket.h>

#include "kvstore/net/binary_protocol.hpp"
#include "kvstore/net/text_protocol.hpp"

namespace kvstore::net::client {

namespace {

bool send_all(int fd, const void* data, size_t len) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = send(fd, ptr + total_sent, len - total_sent, 0);
        if (sent <= 0) {
            return false;
        }
        total_sent += sent;
    }
    return true;
}

std::string read_line(int fd, std::string& buffer) {
    char c;
    while (true) {
        size_t pos = buffer.find('\n');
        if (pos != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }

        ssize_t n = recv(fd, &c, 1, 0);
        if (n <= 0) {
            return "";
        }
        buffer += c;
    }
}

}  // namespace

bool TextProtocolHandler::write_request(int fd, const Request& request) {
    std::string data = TextProtocol::encode_request(request);
    return send_all(fd, data.data(), data.size());
}

std::optional<Response> TextProtocolHandler::read_response(int fd) {
    std::string line = read_line(fd, buffer_);
    if (line.empty() && buffer_.empty()) {
        return std::nullopt;
    }
    return TextProtocol::decode_response(line);
}

bool BinaryProtocolHandler::write_request(int fd, const Request& request) {
    auto data = BinaryProtocol::encode_request(request);
    return send_all(fd, data.data(), data.size());
}

std::optional<Response> BinaryProtocolHandler::read_response(int fd) {
    uint8_t chunk[256];

    while (!BinaryProtocol::has_complete_message(buffer_)) {
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            return std::nullopt;
        }
        buffer_.insert(buffer_.end(), chunk, chunk + n);
    }

    size_t consumed = 0;
    auto resp = BinaryProtocol::decode_response(buffer_, consumed);
    buffer_.erase(buffer_.begin(), buffer_.begin() + consumed);
    return resp;
}

std::unique_ptr<IProtocolHandler> create_protocol_handler(bool binary) {
    if (binary) {
        return std::make_unique<BinaryProtocolHandler>();
    }
    return std::make_unique<TextProtocolHandler>();
}

}  // namespace kvstore::net::client
