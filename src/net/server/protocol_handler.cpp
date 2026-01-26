#include "kvstore/net/server/protocol_handler.hpp"
#include "kvstore/net/binary_protocol.hpp"
#include "kvstore/net/text_protocol.hpp"

#include <sys/socket.h>

namespace kvstore::net::server {
namespace {

bool send_all(int fd, const void* data, size_t len) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t total_sent = 0;
    while(total_sent < len) {
        ssize_t sent = send(fd, ptr + total_sent, len-total_sent, MSG_NOSIGNAL);
        if(sent <= 0) {
            return false;
        }
        total_sent += sent;
    }
    return true;
}

std::string read_line(int fd, std::string& buffer) {
    char chunk [1024];
    
    while(true) {
        size_t pos = buffer.find('\n');
        if(pos != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos+1);
            if(!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }
        ssize_t n = recv(fd, chunk, sizeof(chunk)-1, 0);
        if(n <= 0) {
            return "";
        }
        chunk[n] = '\0';
        buffer += chunk;
    }
}

} //namespace

std::optional<Request> TextProtocolHandler::read_request(int fd) {
    std::string line = read_line(fd, buffer_);
    if(line.empty() && buffer_.empty()) {
        return std::nullopt;
    }
    return TextProtocol::decode_request(line);
}

bool TextProtocolHandler::write_response(int fd, const Response& response) {
    std::string data = TextProtocol::encode_response(response);
    return send_all(fd, data.data(), data.size());
}

std::optional<Request> BinaryProtocolHandler::read_request(int fd) {
    uint8_t chunk[1024];

    while(!BinaryProtocol::has_complete_message(buffer_)) {
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if(n <= 0) {
            return std::nullopt;
        }
        buffer_.insert(buffer_.end(), chunk, chunk+n);
    }

    size_t consumed = 0;
    auto req = BinaryProtocol::decode_request(buffer_, consumed);
    buffer_.erase(buffer_.begin(), buffer_.begin()+consumed);
    return req;
}

bool BinaryProtocolHandler::write_response(int fd, const Response& response) {
    auto data = BinaryProtocol::encode_response(response);
    return send_all(fd, data.data(), data.size());
}

std::unique_ptr<IProtocolHandler> create_protocol_handler(int fd, bool force_binary) {
    if(force_binary) {
        return std::make_unique<BinaryProtocolHandler>();
    }
    
    uint8_t first_byte;
    ssize_t n = recv(fd, &first_byte, 1, MSG_PEEK);

    if(n<=0){
        return nullptr;
    }

    if(first_byte == 0x00 || first_byte > 127) {
        return std::make_unique<BinaryProtocolHandler>();
    }

    return std::make_unique<TextProtocolHandler>();
}


} //namespace kvstore::net::server