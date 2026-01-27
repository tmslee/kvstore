#include "kvstore/core/store.hpp"
#include "kvstore/core/disk_store.hpp"
#include "kvstore/net/server/server.hpp"
#include "kvstore/net/client/client.hpp"

using namespace kvstore;

struct BenchmarkResult {
    std::string name;
    size_t operations;
    double total_seconds;
    double ops_per_second;
    double avg_latency_us;
};

struct LatencyResult {
    std::string name;
    size_t operations;
    double p50_us;
    double p90_us;
    double p99_us;
    double p999_us;
    double max_us;
}

struct MultiThreadResult {
    std::string name;
    size_t num_threads;
    size_t total_operations;
    double total_seconds;
    double ops_per_second;
};

void print_result(const BenchmarkResult& result) {
    std::cout << std::left << std::setw(40) << result.name
              << std::right << std::setw(10) << result.operations << " ops"
              << std::setw(12) << std::fixed << std::setprecision(2) << result.total_seconds << " s"
              << std::setw(12) << std::fixed << std::setprecision(0) << result.ops_per_second << " ops/s"
              << std::setw(10) << std::fixed << std::setprecision(2) << result.avg_latency_us << " us"
              << std::endl;
}

void print_latency(const LatencyResult& result) {
    std::cout << std::left << std::setw(45) << result.name
              << std::right
              << "  p50=" << std::setw(8) << std::fixed << std::setprecision(2) << result.p50_us << " us"
              << "  p90=" << std::setw(8) << std::fixed << std::setprecision(2) << result.p90_us << " us"
              << "  p99=" << std::setw(8) << std::fixed << std::setprecision(2) << result.p99_us << " us"
              << "  p99.9=" << std::setw(8) << std::fixed << std::setprecision(2) << result.p999_us << " us"
              << "  max=" << std::setw(8) << std::fixed << std::setprecision(2) << result.max_us << " us"
              << std::endl;
}

void print_multithread(const MultiThreadResult& result){
    std::cout << std::left << std::setw(45) << result.name
              << std::right
              << "  threads=" << std::setw(2) << result.num_threads
              << "  ops=" << std::setw(10) << result.total_operations
              << "  time=" << std::setw(8) << std::fixed << std::setprecision(2) << result.total_seconds << " s"
              << "  throughput=" << std::setw(10) << std::fixed << std::setprecision(0) << result.ops_per_second << " ops/s"
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

double percentile(std::vector<double>&latencies, double p) {
    if(latencies.empty()) return 0.0;
    size_t idx = static_cast<size_t>(p*latencies.size());
    if(idx >= latencies.size()) idx = latencies.size() - 1;
    return latencies[idx];
}

// =========================================================================================
// store benchmarks (direct, no network)
// =========================================================================================

BenchmarkResult bench_store_put(core::Store& store, size_t count, size_t key_size, size_t value_size) {
    std::mt19937 rng(42);
    std::vector<std::string> keys;
    std::vector<std::string> values;

    keys.reserve(count);
    values.reserve(count);
    for(size_t i=0; i<count; ++i) {
        keys.push_back(random_string(key_size, rng));
        values.push_back(random_string(value_size, rng));
    }

    auto start = Clock::now();
    for(size_t i=0; i < count; ++i) {
        store.put(keys[i], values[i]);
    }
    auto end = Clock::now();

    double seconds = std::chrono::duration<double>(end-start).count();
    return {
        "Store::put (key=" + std::to_string(key_size) + ", val=" + std::to_string(value_size) + ")",
        count,
        seconds,
        count/seconds,
        (seconds*1'000'000)/count
    };
}

BenchmarkResult bench_store_get(core::Store& store, size_t count) {
    auto start = Clock::now();
    for(size_t i=0; i<count; ++i) {
        store.get("key"+std::to_string(i%store.size()));
    }
    auto end = Clock::now();

    double seconds = std::chrono::duration<double>(end-start).count();
    return {
        "Store::get",
        count,
        seconds,
        count/seconds,
        (seconds*1'000'000)/count
    };
}

BenchmarkResult bench_store_mixed(core::Store&store, size_t count, double read_ratio) {
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> ratio_dist(0.0, 1.0);
    std::uniform_int_distribution<size_t> key_dist(0, count-1);

    // populate
    for(size_t i=0; i<count; ++i){
        store.put("key"+std::to_string(i), "value"+std::to_string(i));
    }

    auto start = Clock::now();
    for(size_t i=0; i<count; ++i) {
        if(ratio_dist(rng) < read_ratio) {
            store.get("key" + std::to_string(key_dist(rng)));
        } else {
            store.put("key" + std::to_string(key_dist(rng)), "newvalue");
        }
    }
    auto end = Clock::now();

    double seconds = std::chrono::duration<double>(end - start).count();
    return {
        "Store::mixed (" + std::to_string(static_cast<int>(read_ratio * 100)) + "% reads)",
        count,
        seconds,
        count / seconds,
        (seconds * 1'000'000) / count
    };
}

// =========================================================================================
// client benchmarks (direct, no network)
// =========================================================================================

BenchmarkResult bench_client_put(net::client::Client& client, size_t count, size_t key_size, size_t value_size) {
    std::mt19937 rng(42);
    std::vector<std::string> keys;
    std::vector<std::string> values;

    keys.reserve(count);
    values.reserve(count);
    for(size_t i=0; i<count; ++i) {
        keys.push_back(random_string(key_size, rng));
        values.push_back(random_string(value_size, rng));
    }

    auto start = Clock::now();
    for(size_t i=0; i < count; ++i) {
        client.put(keys[i], values[i]);
    }
    auto end = Clock::now();

    double seconds = std::chrono::duration<double>(end-start).count();
    return {
        "Client::put (key=" + std::to_string(key_size) + ", val=" + std::to_string(value_size) + ")",
        count,
        seconds,
        count/seconds,
        (seconds*1'000'000)/count
    };
}

BenchmarkResult bench_client_get(net::client::Client& client, size_t count) {
    for(size_t i=0; i<1000; ++i) {
        client.put("key"+std::to_string(i), "value"+std::to_string(i));
    }
    
    auto start = Clock::now();
    for(size_t i=0; i<count; ++i) {
        (void) client.get("key"+std::to_string(i%1000));
    }
    auto end = Clock::now();

    double seconds = std::chrono::duration<double>(end-start).count();
    return {
        "Client::get",
        count,
        seconds,
        count/seconds,
        (seconds*1'000'000)/count
    };
}

BenchmarkResult bench_client_ping(net::client::Client& client, size_t count) {
    auto start = Clock::now();
    for(size_t i=0; i<count; ++i) {
        (void) client.ping();
    }
    auto end = Clock::now();

    double seconds = std::chrono::duration<double>(end-start).count();
    return {
        "Client::ping",
        count,
        seconds,
        count/seconds,
        (seconds*1'000'000)/count
    };
}

// =========================================================================================
// diskstore benchmarks
// =========================================================================================

// =========================================================================================
// latency histograms
// =========================================================================================

// =========================================================================================
// multi-threaded client benchmark
// =========================================================================================

// =========================================================================================
// main
// =========================================================================================

int main(int argc, char* argv[]) {
    size_t ops = 100000;
    bool run_network = true;
    bool run_disk = true;
    bool run_latency = true;
    bool run_multithread = true;
    bool use_binary = false;

    for(int i=1; i<argc; ++i) {
        std::string arg = argv[i];
        if(arg == "--ops" && i+1 < argc) {
            ops = std::stoull(argv[++i]);
        } else if(arg == "--no-network") {
            run_network = false;
        } else if(arg == "--no-disk") {
            run_disk = false;
        } else if(arg == "--no-latency") {
            run_latency = false;
        } else if(arg == "--no-multithread") {
            run_multithread = false;
        } else if (arg == "--binary") {
            use_binary = true;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --ops N           number of operatiosn (default: 100000)\n"
                      << "  --no-disk         skip DiskStore benchmarks\n"
                      << "  --no-network      skip network throughput benchmarks\n"
                      << "  --no-latency      skip network latency histogram benchmarks\n"
                      << "  --no-multithread  skip multi-threaded benchmarks\n"
                      << "  --binary          use binary protocol for network tests\n"
                      << "  --help            show this help\n";
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
    
    // DiskStore benchmarks -------------------------------------------------------------


    // network throughput benchmarks -------------------------------------------------------------
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

    // network latency benchmarks -------------------------------------------------------------

    // multithreading benchmarks -------------------------------------------------------------

    std::cout << std::endl;
    std::cout << "Benchmark complete" << std::endl;

    return 0;
}
