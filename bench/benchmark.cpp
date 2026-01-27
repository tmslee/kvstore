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

BenchmarkResult bench_store_put() {}

BenchmarkResult bench_store_get() {}

BenchmarkResult bench_store_mixed() {}

// =========================================================================================
// client benchmarks (direct, no network)
// =========================================================================================

BenchmarkResult bench_client_put() {}

BenchmarkResult bench_client_get() {}

BenchmarkResult bench_client_ping() {}

// =========================================================================================
// main
// =========================================================================================

int main(int argc, char* argv[]) {
    return 0;
}
