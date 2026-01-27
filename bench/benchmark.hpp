#ifndef KVSTORE_BENCH_BENCHMARK_HPP
#define KVSTORE_BENCH_BENCHMARK_HPP

#include <algorithm>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace kvstore::bench {

using Clock = std::chrono::high_resolution_clock;

struct ThroughputResult {
    std::string name;
    size_t operations;
    double total_seconds;
    
    double ops_per_second() const {return operations / total_seconds;}
    double avg_latency_us() const {return (total_seconds * 1'000'000) / operations;}
    
    void print() const {
        std::cout << std::left << std::setw(50) << name
                  << std::right << std::setw(10) << operations << " ops"
                  << std::setw(12) << std::fixed << std::setprecision(2) << total_seconds << " s"
                  << std::setw(12) << std::fixed << std::setprecision(0) << ops_per_second() << " ops/s"
                  << std::setw(10) << std::fixed << std::setprecision(2) << avg_latency_us() << " us"
                  << std::endl;
    }
};

struct LatencyResult {
    std::string name;
    size_t operations;
    std::vector<double> latencies_us;

    double percentile(double p) const {
        if(latencies_us.empty()) return 0.0;
        size_t idx = static_cast<size_t>(p*latencies_us.size());
        if(idx >= latencies_us.size()) idx = latencies_us.size()-1;
        return latencies_us[idx];
    }

    void sort() {
        std::sort(latencies_us.begin(), latencies_us.end());
    }

    void print() const {
        std::cout << std::left << std::setw(50) << name
                  << std::right << std::setw(10) << operations << " ops"
                  << std::setw(12) << std::fixed << std::setprecision(2) << total_seconds << " s"
                  << std::setw(12) << std::fixed << std::setprecision(0) << ops_per_second() << " ops/s"
                  << std::setw(10) << std::fixed << std::setprecision(2) << avg_latency_us() << " us"
                  << std::endl;
    }
};

struct MultiThreadResult {
    std::string name;
    size_t num_threads;
    size_t total_operations;
    double total_seconds:
    double ops_per_second() const {return total_operations / total-seconds; }
    void print() const {
        std::cout << std::left << std::setw(50) << name
                  << std::right
                  << "  threads=" << std::setw(2) << num_threads
                  << "  ops=" << std::setw(10) << total_operations
                  << "  time=" << std::setw(8) << std::fixed << std::setprecision(2) << total_seconds << " s"
                  << "  throughput=" << std::setw(10) << std::fixed << std::setprecision(0) << ops_per_second() << " ops/s"
                  << std::endl;
    }
};

class Benchmark {

};

// utilities
class RandomGenerator {

};

class DataSet {

};

inline void print_header(const std::string& title) {
    std::cout << "--- " << title << " ---" << std::endl;
}

} // namespace kvstore::bench

#endif