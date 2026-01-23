#include <iostream>

#include "kvstore/core/store.hpp"
#include "kvstore/net/server.hpp"
#include "kvstore/util/signal_handler.hpp"

int main(int argc, char* argv[]) {
    try {
        uint16_t port = 6379;
        std::string data_dir = "./data";

        //arg parse
        for(int i=1; i<argc; ++i) {
            std::string arg = argv[i];
            if((arg == "-p" || arg == "--[port]") && i+1 < argc) {
                port = static_cast<uint16_t>(std::stoi(argv[++i]));
            } else if ((arg == "-d" || arg == "--data-dir") && i+1 < argc) {
                data_dir = argv[++i];
            } else if ((arg == "-h") || arg == "--help") {
                std::cout << "Usage: " << argv[0] << " [options]\n"
                    << "Options:\n"
                    << "  -p, --port PORT      Port to listen on (default: 6379)\n"
                    << "  -d, --data-dir DIR   Data directory (default: ./data)\n"
                    << "  -h, --help           Show this help\n";
                return 0;
            }
        }

        //set up store with persistence
        std::filesystem::create_directories(data_dir);
        kvstore::core::StoreOptions store_opts;
        store_opts.persistence_path = std::filesystem::path(data_dir) / "store.wal";
        store_opts.snapshot_path = std::filesystem::path(data_dir) / "store.snap";
        store_opts.snapshot_threshold = 10000;
        kvstore::core::Store store(store_opts);

        //setup server
        kvstore::net::ServerOptions server_opts;
        server_opts.port = port;
        kvstore::net::Server server(store, server_opts);

        //install signal handlers
        kvstore::util::SignalHandler::install();

        //start server   
        server.start();
        std::cout << "Server listening on port " << port << std::endl;
        std::cout << "Press Ctrl+C to shutdown" << std::endl;

        //wait for shutdown signal
        kvstore::util::SignalHandler::wait_for_shutdown();
        std::cout << "\nShutting down..." << std::endl;

        // stop server (drains connections)
        server.stop();

        
        /*
            note on manual shutdown
            - manual shutdown
                - pros:
                    explicit: caller knows whats happening
                    flexible: maybe caller doesnt want snapshot
                    store doesnt know about shutdown.
                - cons:
                    easy to forget
                    caller must understand internals

            - auto shutdown
                - pros: 
                    cant forget, caller must understand internals
        */
        //flush store (snapshot)
        std::cout << "Saving snapshot..." << std::endl;
        store.snapshot();

        std::cout << "Shutdown complete" << std::endl;
        
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    
}