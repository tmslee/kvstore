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
        std::cout << std::left << std::setw(25) << name
                  << std::right << std::setw(10) << operations << " ops"
                  << "  elapsed time=" << std::fixed << std::setprecision(2) << total_seconds << " s"
                  << "  throughput=" << std::fixed << std::setprecision(0) << ops_per_second() << " ops/s"
                  << "  avg latency=" << std::fixed << std::setprecision(2) << avg_latency_us() << " us"
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
        std::cout << std::left << std::setw(25) << name
                  << std::right
                  << "  p50=" << std::fixed << std::setprecision(2) << percentile(0.50) << " us"
                  << "  p90=" << std::fixed << std::setprecision(2) << percentile(0.90) << " us"
                  << "  p99=" << std::fixed << std::setprecision(2) << percentile(0.99) << " us"
                  << "  p99.9=" << std::fixed << std::setprecision(2) << percentile(0.999) << " us"
                  << "  max=" << std::fixed << std::setprecision(2) << latencies_us.back() << " us"
                  << std::endl;
    }
};

struct MultiThreadResult {
    std::string name;
    size_t num_threads;
    size_t total_operations;
    double total_seconds;
    double ops_per_second() const {return total_operations / total_seconds; }
    void print() const {
        std::cout << std::left << std::setw(25) << name
                  << std::right
                  << "  threads=" << num_threads
                  << "  ops=" << total_operations
                  << "  time=" << std::fixed << std::setprecision(2) << total_seconds << " s"
                  << "  throughput=" << std::fixed << std::setprecision(0) << ops_per_second() << " ops/s"
                  << std::endl;
    }
};

class Benchmark {
public:
    using Operation = std::function<void()>;

    explicit Benchmark(std::string name) : name_(std::move(name)) {}

    ThroughputResult run_throughput(size_t count, Operation op) {
        auto start = Clock::now();
        for(size_t i=0; i<count; ++i) {
            op();
        }
        auto end = Clock::now();
        double seconds = std::chrono::duration<double>(end-start).count();
        return {name_, count, seconds};
    }

    LatencyResult run_latency(size_t count, Operation op) {
        LatencyResult result{name_, count, {}};
        result.latencies_us.reserve(count);
        for(size_t i=0; i<count; ++i) {
            auto start = Clock::now();
            op();
            auto end = Clock::now();
            result.latencies_us.push_back(
                std::chrono::duration<double, std::micro>(end-start).count()
            );
        }
        result.sort();
        return result;
    }

private:
    std::string name_;
};

// utilities
class RandomGenerator {
public:
    explicit RandomGenerator(uint32_t seed = 42) : rng_(seed) {}

    std::string string(size_t length) {
        static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
        std::string result(length, '\0');
        for (size_t i = 0; i < length; ++i) {
            result[i] = charset[dist(rng_)];
        }
        return result;
    }

    size_t uniform(size_t min, size_t max) {
        std::uniform_int_distribution<size_t> dist(min, max);
        return dist(rng_);
    }

    double uniform_real(double min = 0.0, double max = 1.0) {
        std::uniform_real_distribution<double> dist(min, max);
        return dist(rng_);
    }

private:
    std::mt19937 rng_;
};

class DataSet {
public:
    DataSet(size_t count, size_t key_size, size_t value_size, uint32_t seed = 42) {
        RandomGenerator rng(seed);
        keys_.reserve(count);
        values_.reserve(count);
        for(size_t i=0; i<count; i++) {
            keys_.push_back(rng.string(key_size));
            values_.push_back(rng.string(value_size));
        }
    }

    const std::string& key(size_t i) const {return keys_[i%keys_.size()];}
    const std::string& value(size_t i) const {return values_[i%values_.size()];}
    size_t size() const {return keys_.size();}

private:
    std::vector<std::string> keys_;
    std::vector<std::string> values_;
};

inline void print_header(const std::string& title) {
    std::cout << "--- " << title << " ---" << std::endl;
}

} // namespace kvstore::bench

#endif