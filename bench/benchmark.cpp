#include "benchmark.hpp"
#include "kvstore/core/store.hpp"
#include "kvstore/core/disk_store.hpp"
#include "kvstore/net/server/server.hpp"
#include "kvstore/net/client/client.hpp"

#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

using namespace kvstore;
using namespace kvstore::bench;

//=========================================================================================
// store benchmarks
// =========================================================================================
void bench_store(core::Istore& store, const std::string& store_name, size_t ops){
    print_header(store_name);

    //PUT small values
    {
        DataSet data(ops, 16, 64);
        size_t i = 0;
        Benchmark("put (key=16, val=64)")
            .run_throughput(ops, [&]() {
                store.put(data.key(i), data.value(i)); 
                ++i;
            })
            .print();
        store.clear();
    }

    //PUT large values
    {
        DataSet data(ops, 16, 1024);
        size_t i = 0;
        Benchmark("put (key=16, val=1024)")
            .run_throughput(ops, [&](){
                store.put(data.key(i), data.value(i));
                ++i;
            })
            .print();
        store.clear();
    }

    //GET
    {
        for(size_t i=0; i<ops; ++i) {
            store.put("key" + std::to_string(i), "value" + std::to_string(i));
        }
        size_t i = 0;
        Benchmark("get")
            .run_throughput(ops, [&]() {
                store.get("key" + std::to_string(i % ops));
                ++i;
            });
            .print();
        store.clear();
    }

    // mixed workload
    {
        RandomGenerator rng;
        for(size_t i=0; i<ops; ++i) {
            store.put("key" + std::to_string(i), "value" + std::to_string(i));
        }
        Benchmark("mixed (80% reads)")
            .run_throughput(ops, [&]() {
                size_t k = rng.uniform(0, ops-1);
                if(rng.uniform_real() < 0.8) {
                    store.get("key" + std::to_string(k));
                } else {
                    store.put("key" + std::to_string(k), "newvalue");
                }
            })
            .print();
        store.clear();
    }

    std::cout << std::endl;
}

//=========================================================================================
// network benchmarks
// =========================================================================================
void bench_network_throughput(net::client::Client& client, core::Store& store, size_t ops) {
    // PING
    Benchmark("ping")
        .run_throughput(ops, [&](){
            client.ping();
        })
        .print();

    // PUT small
    {
        DataSet data(ops, 16, 64);
        size_t i = 0;
        store.clear();
        Benchmark("put (key=16, val=64)")
            .run_throughput(ops, [&]() { 
                client.put(data.key(i), data.value(i)); 
                ++i; 
            })
            .print();
    }

    // PUT large
    {
        DataSet data(ops, 16, 1024);
        size_t i = 0;
        store.clear();
        Benchmark("put (key=16, val=1024)")
            .run_throughput(ops, [&]() { 
                client.put(data.key(i), data.value(i)); 
                ++i; 
            })
            .print();
    }

    // GET
    {
        store.clear();
        for (size_t i = 0; i < 1000; ++i) {
            client.put("key" + std::to_string(i), "value" + std::to_string(i));
        }
        size_t i = 0;
        Benchmark("get")
            .run_throughput(ops, [&]() { 
                client.get("key" + std::to_string(i % 1000)); 
                ++i; 
            })
            .print();
    }
}

void bench_network_latency(net::client::Client& client, core::Store& store, size_t ops) {
    // PING
    Benchmark("ping")
        .run_latency(ops, [&](){
            client.ping();
        })
        .print();

    // PUT small
    {
        DataSet data(ops, 16, 64);
        size_t i = 0;
        store.clear();
        Benchmark("put (key=16, val=64)")
            .run_latency(ops, [&]() { 
                client.put(data.key(i), data.value(i)); 
                ++i; 
            })
            .print();
    }

    // PUT large
    {
        DataSet data(ops, 16, 1024);
        size_t i = 0;
        store.clear();
        Benchmark("put (key=16, val=1024)")
            .run_latency(ops, [&]() { 
                client.put(data.key(i), data.value(i)); 
                ++i; 
            })
            .print();
    }

    // GET
    {
        store.clear();
        for (size_t i = 0; i < 1000; ++i) {
            client.put("key" + std::to_string(i), "value" + std::to_string(i));
        }
        size_t i = 0;
        Benchmark("get")
            .run_latency(ops, [&]() { 
                client.get("key" + std::to_string(i % 1000)); 
                ++i; 
            })
            .print();
    }
}

//=========================================================================================
// multi threaded benchmarks
// =========================================================================================
MultiThreadResult bench_multithread(
    const std::string& name,
    net::server::Server& server,
    size_t num_threads,
    size_t ops_per_thread,
    pool binary,
    std::function<void(net::client::Client&, size_t)> worker_fn
) {
    std::vector<std::thread> threads;

    auto start = Clock::now();
    for(size_t t=0; t < num_threads; ++t) {
        threads.emplace_back([&, t](){
            net::client::ClientOptions opts;
            opts.port = server.port();
            opts.binary = binary;

            net::client::Client client(opts);
            client.connect();

            worker_fn(client, ops_per_thread);

            client.disconnect();
        });
    }

    for(auto& th : threads) {
        th.join();
    }
    auto end = Clock::now();

    double seconds = std::chrono::duration<double>(end - start).count();
    return {name, num_threads, num_threads*ops_per_thread, seconds};
}

void bench_multithread_scaling(
    net::server::Server& server,
    core::Store& store,
    size_t ops_per_thread,
    bool binary
) {
    for(size_t threads : {1, 2, 4, 8}) {
        store.clear();
        DataSet data(ops_per_thread, 16, 64);

        bench_multithread(
            "put (key=16, val=64)",
            server, threads, ops_per_thread, binary,
            [&data](net::client::Client& client, size_t ops) {
                for(size_t i=0; i<ops; ++i) {
                    client.put(data.key(i), data.value(i));
                }
            }
        ).print();
    }

    std::cout << std::endl;

    //pre populate for mixed workload
    store.clear();
    {
        net::client::ClientOptions opts;
        opts.port = server.port();
        opts.binary = binary;
        net::client::Client client(opts);
        client.connect();
        for (size_t i = 0; i < 10000; ++i) {
            client.put("key" + std::to_string(i), "value" + std::to_string(i));
        }
        client.disconnect();
    }

    for (size_t threads : {1, 2, 4, 8}) {
        bench_multithread(
            "mixed (80% reads)",
            server, threads, ops_per_thread, binary,
            [](net::client::Client& client, size_t ops) {
                RandomGenerator rng;
                for (size_t i = 0; i < ops; ++i) {
                    size_t k = rng.uniform(0, 9999);
                    if (rng.uniform_real() < 0.8) {
                        client.get("key" + std::to_string(k));
                    } else {
                        client.put("key" + std::to_string(k), "newvalue");
                    }
                }
            }
        ).print();
    }
}

//=========================================================================================
// protocol comparison
// =========================================================================================
void bench_protocol_comparison(net::server::Server& server, core::Store& store, size_t ops) {
    DataSet data(ops, 16, 64);

    // Text
    {
        net::client::ClientOptions opts;
        opts.port = server.port();
        opts.binary = false;
        net::client::Client client(opts);
        client.connect();

        store.clear();
        size_t i = 0;
        auto result = Benchmark("text: put (key=16, val=64)")
            .run_throughput(ops, [&]() { client.put(data.key(i), data.value(i)); ++i; });
        result.print();

        client.disconnect();
    }

    // Binary
    {
        net::client::ClientOptions opts;
        opts.port = server.port();
        opts.binary = true;
        net::client::Client client(opts);
        client.connect();

        store.clear();
        size_t i = 0;
        auto result = Benchmark("binary: put (key=16, val=64)")
            .run_throughput(ops, [&]() { client.put(data.key(i), data.value(i)); ++i; });
        result.print();

        client.disconnect();
    }
}

//=========================================================================================
// main
// =========================================================================================
int main(int argc, char* argv[]) {
    size_t ops = 100000;
    bool run_network = true;
    bool run_disk = true;
    bool run_latency = true;
    bool run_multithread = true;
    bool run_comparison = true;
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
        } else if (arg == "--no-comparison") {
            run_comparison = true;
        } else if (arg == "--binary") {
            use_binary = true;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --ops N           number of operatiosn (default: 100000)\n"
                      << "  --no-disk         skip DiskStore benchmarks\n"
                      << "  --no-network      skip network benchmarks\n"
                      << "  --no-latency      skip latency histogram benchmarks\n"
                      << "  --no-multithread  skip multi-threaded benchmarks\n"
                      << "  --no-comparison   skip protocol comparison\n"
                      << "  --binary          use binary protocol for network tests\n"
                      << "  --help            show this help\n";
            return 0;
        }           
    }

    std::cout << "=== KVstore Benchmark ===" << std::endl;
    std::cout << "Operations per tests: " << ops << std::endl;
    std::cout << std::endl;

    // in memory store
    {
        core::Store store;
        bench_store(store, "Store (in-memory)", ops);
    }

    // disk store
    if(run_disk) {
        auto temp_dir = std::filesystem::temp_directory_path() / "kvstore_bench";
        std::filesystem::remove_all(temp_dir);
        std::filesystem::create_directories(temp_dir);

        core::DiskStoreOptions disk_opts;
        disk_opts.data_dir = temp_dir;
        core::DiskStore store(disk_opts);

        bench_store(store, "DiskStore", ops/10);
        
        std::filesystem::remove_all(temp_dir);
    }

    // network benchmarks
    if(run_network) {
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

        std::string protocol_name = use_binary ? "binary" : "text";

        //throughput
        print_header("Network throughput (" + protocol_name + ")");
        bench_network_throughput(client, store, ops);
        std::cout << std::endl;
    
        if(run_latency) {
            print_header("Network latency (" + protocol_name + ")");
            bench_network_latency(client, store, std::min(ops, size_t(10000)));
            std::cout << std::endl;
        }

        client.disconnect();

        //multi thread
        if(run_multithread) {
            print_header("Multi-threaded (" + protocol_name + ")");
            bench_multithread_scaling(server, store, ops/10, use_binary);
            std::cout << std::endl;
        }

        //protocol comparison
        if(run_comparison && !use_binary) {
            print_header("Protocol comparison");
            bench_protocol_comparison(server, store, ops / 2);
            std::cout << std::endl;
        }
        
        server.stop();
    } 

    std::cout << "Benchmark complete" << std::endl;

    return 0;
}
