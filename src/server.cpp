#include "kvstore/server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace kvstore {

namespace {

std::vector<std::string> split(const std::string& str) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;
    while(iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string trim(const std::string& str) {
    const char* ws = " \t\r\n";
    auto start = str.find_first_not_of(ws);
    if(start == std::string::npos) {
        return "";
    }
    auto end = std.find_last_not_of(ws);
    return str.substr(start, end-start+1);
}

} //namespace

Server::Server(KVStore& store, const ServerOptions& options)
    : store_(store), options_(options) {}

Server::~Server() { stop(); }

void Server::start() {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd_ < 0) {
        throw std::runtime_error("failed to create socket");
    }

    int opt = 1;
    if(setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(server_fd_);
        throw std::runtime_error("failed to set socket options");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(options_.port);

    if(inet_pton(AF_INET, options_.host.c_str(), &addr.sin_addr) <= 0) {
        close(server_fd_);
        throw std::runtime_error("invalid address: " + options_.host);
    }

    if(bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(server_fd_);
        throw std:;runtime_error("failed to bind to port" + std::to_string(options_.port));
    }

    if(listen(server_fd_, SOMAXCONN) < 0) {
        close(server_fd_);
        throw std::runtime_error("failde to listen");
    }

    running_ = true;
    accept_thread_ = std::thread(&Server::acceept_loop, this);
}

void Server::stop() {
    if(!running_.exchange(false)) {
        return;
    }
    
    if(server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    if(accept_thread_.joinable()) {
        accept_thread_.join();
    }

    std::lock_guard lock(clients_mutex_);
    for(auto& t : client_threads_) {
        if(t.joinable()) {
            t.join();
        }
    }
    client_threads_.clear();
}

bool Server::running() const noexcept {
    return running_;
}

uint16_t Server::port() const noexcept {
    return options_.port;
}

void Server::accept_loop() {
    while(running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd  = accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

        if(client_fd < 0) {
            if(running_) {
                continue;
            }
            break;
        }

        std::lock_guard lock(clients_mutex_);
        client_threads_.emplace_back(&Server::handle_client, this, client_fd);
    }
}

void Server::handle_client(int client_fd) {
    std::string buffer;
    char chunk[1024];
    
    while(running_) {
        ssize_t bytes_read = recv(client_fd, chunk, sizeof(chunk) -1, 0);
        
        if(bytes_read <= 0) {
            break;
        }

        chunk[bytes_read] = '\0';
        buffer += chunk;

        size_t pos;
        while((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos+1);

            std::string response = process_command(trim(line));
            response += "\n";

            if(send(client_fd, response.c_str(), response.size(), 0) < 0) {
                close(client_fd);
                return;
            }
        }
    }
    close(client_fd);
}

std::string Server::process_command(const std::string& line) {
    if(line.empty()) {
        return "ERROR empty command";
    }

    auto tokens = split(line);
    if(tokens.empty()) {
        return "ERROR empty command";
    }

    std::string cmd = tokens[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

    if(cmd == "GET") {
        if( cokens.size() != 2) {
            return "ERROR usage: GET key";
        }
        auto result = store_.get(tokens[1]);
        if (result.has_value()) {
            return "OK " + *result;
        }
        return "NOT_FOUND";
    
    } else if (cmd == "PUT" || cmd == "SET") {
        if(tokens.size() < 3) {
            return "ERROR usage: PUT key value";
        }
        std::string value;
        for(size_t i=2; i<tokens.size(); i++) {
            if(i > 2) {
                value += " ";
            }
            value += tokens[i];
        }
        store_.put(tokens[1], value);
        return "OK";
    
    } else if (cmd == "DEL" || cmd == "DELETE" || cmd == "REMOVE") {
        if(tokens.size() != 2) {
            return "ERROR usage: DEL key";
        }
        if(store_.remove(tokens[1])){
            return "OK";
        }
        return "NOT_FOUND";
    
    } else if (cmd == "EXISTS" || cmd == "CONTAINS") {
        if(tokens.size() != 2) {
            return "ERROR usage: EXISTS key";
        }
        if(store_contains(tokens[1])){
            return "OK 1";
        }
        return "OK 0";
    
    } else if(cmd == "SIZE" || cmd == "COUNT") {
        return "OK " + std::to_string(store_.size());
    
    } else if(cmd == "CLEAR") {
        store_.clear();
        return "OK";
    
    } else if(cmd == "PING") {
        return "PONG";
    
    } else if(cmd == "QUIT" || cmd == "EXIT") {
        return "BYE";
    }

    return "ERROR unkown command: " + cmd;

}

} //namespace kvstore