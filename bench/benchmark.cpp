#include "kvstore/core/store.hpp"
#include "kvstore/core/disk_store.hpp"
#include "kvstore/net/server/server.hpp"
#include "kvstore/net/client/client.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace kvstore;
using Clock = std::chrono::high_resolution_clock;

struct BenchmarkResult {
    std::string name;
    size_t operations;
    double total_seconds;
    double ops_per_second;
    double avg_latency_us;
};

void print_result(const BenchmarkResult& result) {
    std::cout << std::left << std::setw(40) << result.name
              << std::right << std::setw(10) << result.operations << " ops"
              << std::setw(12) << std::fixed << std::setprecision(2) << result.total_seconds
              << std::setw(12) << std::fixed << std::setprecision(0) << result.ops_per_second
              << std::setw(10) << std::fixed << std::setprecision(2) << result.avg_latency_us
              << std::endl;
}

std::string random_string(size_t length, std::mt19937& rng) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    std::string result(length, '\0');
    for(size_t i=0; i<length; ++i) {
        result[i] = charset[dist(rng)];
    }
    return result;
}

// =========================================================================================
// store benchmarks (direct, no network)
// =========================================================================================

BenchmarkResult bench_store_put(core::Store& store, size_t count, size_t key_size, size_t value_size) {}

BenchmarkResult bench_store_get(core::Store& store, size_t count) {}

BenchmarkResult bench_store_mixed(core::Store&store, size_t count, double read_ratio) {}

// =========================================================================================
// client benchmarks (direct, no network)
// =========================================================================================

BenchmarkResult bench_client_put(net::client::Client& client, size_t count, size_t key_size, size_t value_size) {}

BenchmarkResult bench_client_get(net::client::Client& client, size_t count) {}

BenchmarkResult bench_client_ping(net::client::Client& client, size_t count) {}

// =========================================================================================
// main
// =========================================================================================

int main(int argc, char* argv[]) {
    size_t ops = 100000;
    bool run_network = true;
    bool use_binary = false;

    for(int i=1; i<argc; ++i) {
        std::string arg = argv[i];
        if(arg == "--ops" && i+1 < argc) {
            ops = std::stoull(argv[++i]);
        } else if(arg == "--no-network") {
            run_network = false;
        } else if (arg == "--binary") {
            use_binary = true;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --ops N         number of operatiosn (default: 100000)\n"
                      << "  --no-network    skip network benchmarks\n"
                      << "  --binary        use binary protocol for network tests\n"
                      << "  --help          show this help\n";
            return 0;
        }           
    }

    std::cout << "=== KVstore Benchmark ===" << std::endl;
    std::cout << "Operations per tests: " << ops << std::endl;
    std::cout << std::endl;

    // direct store benchmarks ----------------------------------------------------------
    std::cout << "--- Direct Store (no network) ---" << std::endl;
    {
        core::Store store;
        print_result(bench_store_put(store, ops, 16, 64));
        store.clear();

        print_result(bench_store_put(store, ops, 16, 1024));
        store.clear();

        //populate for get test
        for(size_t i=0 ; i<ops; ++i) {
            store.put("key"+std::to_string(i), "value"+std::to_string(i));
        }
        print_result(bench_store_get(store, ops));
        store.clear();

        print_result(bench_store_mixed(store, ops, 0.8));
        store.clear();

        print_result(bench_store_mixed(store, ops, 0.5));
    }
    std::cout << std::endl;

    // network benchmarks -------------------------------------------------------------
    if(run_network) {
        std::cout << "--- Network (" << (use_binary ? "binary" : "text") << " protocol) ---" << std::endl;

        core::Store store;

        net::server::ServerOptions server_opts;
        server_opts.port = 0;
        net::server::Server server(store, server_opts);
        server.start();

        net::client::ClientOptions client_opts;
        client_opts.port = server.port();
        client_opts.binary = use_binary;
        net::client::Client client(client_opts);
        client.connect();

        print_result(bench_client_ping(client, ops));

        store.clear();
        print_result(bench_client_put(client, ops, 16, 64));

        store.clear();
        print_result(bench_client_put(client, ops, 16, 1024));

        store.clear();
        print_result(bench_client_get(client, ops));

        client.disconnect();
        server.stop();
    }

    std::cout << std::endl;
    std::cout << "Benchmark complete" << std::endl;

    return 0;
}
