#include "kvstore/kvstore.hpp"
#include "kvstore/server.hpp"

#include <csignal>
#include <iostream>

namespace {
kvstore::Server* g_server = nullptr;

void signal_handler(int) {
    if(g_server != nullptr) {
        g_server->stop();
    }
}

}// namespace

int main() {
    kvstore::Options store_opts;
    store_opts.persistence_path = "data.wal";

    kvstore::KVStore store(store_opts);

    kvstore::ServerOptions server_opts;
    server_opts.port = 6379;

    kvstore::Server server(store, server_opts);
    g_server = &server;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "Starting kvstore server on port " << server_opts.port << "...\n";
    server.start();
    std::cout << "Server running. press Ctrl+C to stop.\n";

    while(server.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Server stopped.\n";
    return 0;
}