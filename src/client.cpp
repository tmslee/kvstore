#include "kvstore/client.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stdexcept>

namespace kvstore {

Client::Client(const ClientOptions& options) : options_(options) {}

Client::~Client() {
    disconnect();
}

Client::Client(Client&& other) noexcept
    : options_(std::move(other.options_)), socket_fd_(other.socket_fd_) 
{
    other.socket_fd_ = -1;
}

Client& Client::operator=(Client&& other) noexcept {
    if(this != &other) {
        disconnect();
        options_ = std::move(other.options_);
        socket_fd_ = other.socket_fd_;
        other.socket_fd_ = -1;
    }
    return *this;
}

void Client::connect(){}

void Client::disconnect(){}

bool Client::connected() const noexcept {}

std::string Client::send_command(const std::string& command) {}

std::string Client::read_response() {}

void Client::put(std::string_view key, std::string_view value) {}

std::optional<std::string> Client::get(std::string_view key) {}

bool Client::remove(std::string_view key) {}

bool Client::contains(std::string_view key) {}

std::size_t Client::size() {}

void Client::clear() {}

bool Client::ping() {}

} //namespace kvstore