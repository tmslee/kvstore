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

std::vector<std::string> split(const std::string& str) {}

std::string trim(const std::string& str) {}

} //namespace

Server::Server(KVStore& store, const ServerOptions& options)
    : store_(store), options_(options) {}

Server::~Server() {}

void Server::start() {}

void Server::stop() {}

bool Server::running() const noexcept {}

uint16_t Server::port() const noexcept {}

void Server::accept_loop() {}

void Server::handle_client(int client_fd) {}

std::string ServeR::process_command(const std::string& line) {}

} //namespace kvstore