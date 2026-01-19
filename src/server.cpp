#include "kvstore/server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>

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
    auto end = str.find_last_not_of(ws);
    return str.substr(start, end-start+1);
}

} //namespace

Server::Server(KVStore& store, const ServerOptions& options)
    : store_(store), options_(options) {}

Server::~Server() { 
    // destructor should NOT throw.
    // but stop() CAN throw -> refer to the method implementation
    // here we log and swallow 
    try {
        stop(); 
    } catch (const std::exception& e) {
        std::cerr << "Server::~Server: stop() failed: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "Server::~Server: stop() failed with unkown error\n";
    }
    
}

void Server::start() {
    /*
        1. create socket with protocols (IPv4 & TCP)
        2. set socket options
        3. set up address struct
        4. bind address&port to socket
        5. mark for listen
        6. spawn thread to accept connections
    */ 

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    // AF_INET = IPv4, SOCK_STREAM = TCP
    // socket sctor returns a file descriptor (integer handle)
    // negative means error
    if(server_fd_ < 0) {
        throw std::runtime_error("failed to create socket");
    }

    int opt = 1;
    // SO_REUSEADDR lets us rebind to port immediately after restart. without it you get "address already in use" for ~30s after stopping
    /*
        SOL_SOCKET speicifed level at which option is defined:
        SOL_SOCKET (generic socket layer) : SO_REUSEADDR, SO_KEEPALIVE, SO_RCVBUF ...
        IPPROTO_TCP (TCP protocol layer) : TCP_NODELAY, TCP_KEEPIDLE ...
        IPPROTO_IP (IP protocol layer) : IP_TTL, IP_MULTICAST_IF ...
    */
    if(setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(server_fd_);
        throw std::runtime_error("failed to set socket options");
    }

    //set up address struct. htons conver port to network byte order (big-endian)
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(options_.port);

    //inet_pton (inet presentation to network) converts string IP("127.0.0.1") to binary format & store in addr.sin_addr
    if(inet_pton(AF_INET, options_.host.c_str(), &addr.sin_addr) <= 0) {
        close(server_fd_);
        throw std::runtime_error("invalid address: " + options_.host);
    }

    // bind associated socket with specific address and port
    // before bind, socket exists but has no address -> OS doesnt know where to route incoming packets
    // after bind, socket bound to 127.0.0.1:6379. OS routes packets for that addr/port to this socket
    if(bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(server_fd_);
        throw std::runtime_error("failed to bind to port" + std::to_string(options_.port));
    }
    
    // mark socket as listening - SOMAXCONN is max backlog of pending connections
    if(listen(server_fd_, SOMAXCONN) < 0) {
        close(server_fd_);
        throw std::runtime_error("failed to listen");
    }

    // set running flag, spawn thread to accept connections
    running_ = true;
    accept_thread_ = std::thread(&Server::accept_loop, this);
}

void Server::stop() {
    // exchange sets the value as argument and returns old value
    // if already stopped, return early.
    if(!running_.exchange(false)) {
        return;
    }
    
    // shutdown stops reads and writes, unblocking any threads stuck in accept()
    // close releases fd
    if(server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR); // can fail, but we ignore
        close(server_fd_); //can fail, but we ignroe
        server_fd_ = -1;
    }

    // wait for accept thread to finish
    if(accept_thread_.joinable()) {
        accept_thread_.join(); //noexcept
    }

    // wait for all client handler threds to finish, then clear vector.
    std::lock_guard lock(clients_mutex_); //can throw std::system_error
    for(auto& t : client_threads_) {
        if(t.joinable()) {
            t.join(); //noexcept
        }
    }
    client_threads_.clear();

    // std::lock_guard ctor can technically throw. rare but possible.
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
            if(running_) { //retry if we're still running
                continue;
            }
            break; //exit if we're done
        }
        {
            std::lock_guard lock(clients_mutex_);
            client_threads_.emplace_back(&Server::handle_client, this, client_fd);
        }
    }
}

void Server::handle_client(int client_fd) {
    std::string buffer;
    char chunk[1024];

    while(running_) {
        ssize_t bytes_read = recv(client_fd, chunk, sizeof(chunk) -1, 0);
        //0 = client closed connection, negative = error.
        if(bytes_read <= 0) {
            break;
        }

        // null-terminate the chunk @ end of bytes read and append to buffer
        // chunk is a C-string: without null term it would read past valid data into garbage memory. alternatively, buffer.append(chunk, bytes_read);
        /*
            IMPORTANT NOTE: network APIs are C functions. we use c-strings (null terminated character) because:
                - direct compatibility with syscalls
                - no heap allocation for small buffers
                - explicit control over memory layout
        */
        chunk[bytes_read] = '\0';
        buffer += chunk;

        size_t pos;
        // while we have complete lines in our buffer, extract the line from buffer (1 full command) and process it.
        while((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos+1);
            
            // append newline so client can recognize end of response
            std::string response = process_command(trim(line));
            response += "\n";
            // send response. if fail, close and exit.
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
        if(tokens.size() != 2) {
            return "ERROR usage: GET key";
        }
        auto result = store_.get(tokens[1]);
        if(result.has_value()) {
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
        if(store_.contains(tokens[1])){
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