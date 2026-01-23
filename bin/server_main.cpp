#include <iostream>

#include "kvstore/core/store.hpp"
#include "kvstore/core/disk_store.hpp"
#include "kvstore/net/server.hpp"
#include "kvstore/util/signal_handler.hpp"
#include "kvstore/util/logger.hpp"
#include "kvstore/util/config.hpp"

int main(int argc, char* argv[]) {
    try {
        kvstore::util::Config defaults;
        kvstore::util::Config file_config = defaults;
        kvstore::util::Config cli_config = defaults;

        //first pass: find config_path;
        std::filesystem::path config_path;
        for(int i=1; i<argc; ++i) {
            std::string arg = argv[i];
            if((arg == "-c" || arg == "--config") && i+1 < argc) {
                config_path = argv[++i];
                break;
            }
        }

        //load config file if specified
        if(!config_path.empty()) {
            auto loaded = kvstore::util::Config::load_file(config_path);
            if(loaded) {
                file_config = *loaded;
            } else {
                std::cerr << "Warning: Could not load config file: " << config_path << std::endl;
            }
        }

        auto cli_result = kvstore::util::Config::parse_args(argc, argv);
        if(!cli_result) {
            return 0; //--help was shown
        }
        cli_config = *cli_result;

        //Merge: CLI > file > defaults
        auto config = kvstore::util::Config::merge(file_config, cli_config, defaults);

        //setup logger
        kvstore::util::Logger::instance().set_level(config.log_level);

        //create data dir
        std::filesystem::create_directories(config.data_dir);

        //setup store
        std::unique_ptr<kvstore::core::IStore> store;

        if(config.use_disk_store) {
            kvstore::core::DiskStoreOptions opts;
            opts.data_dir = config.data_dir;
            opts.compaction_threshold = config.compaction_threshold;
            store = std::make_unique<kvstore::core::DiskStore>(opts);
            LOG_INFO("Using disk-based storage");
        } else {
            kvstore::core::StoreOptions opts;
            opts.persistence_path = config.data_dir / "store.wal";
            opts.snapshot_path = config.data_dir / "store.snap";
            opts.snapshot_threshold = config.snapshot_threshold;
            store = std::make_unique<kvstore::core::Store>(opts);
            LOG_INFO("Using in-memory storage with WAL");
        }

        //setup server
        kvstore::net::ServerOptions server_opts;
        server_opts.host = config.host;
        server_opts.port = config.port;
        server_opts.max_connections = config.max_connections;
        server_opts.client_timeout_seconds = config.client_timeout_seconds;

        kvstore::net::Server server(*store, server_opts);

        //install signal handlers
        kvstore::util::SignalHandler::install();

        //start server
        server.start();

        LOG_INFO("Press Ctrl+C to shutdown");

        //wait for shutdown signal
        kvstore::util::SignalHandler::wait_for_shutdown();

        //stop server
        server.stop();

        //flush store (snapshot for Store, compact for DiskStore)
        LOG_INFO("Flushing store...");
        store->flush();

        LOG_INFO("Shutdown complete");
        return 0;
    
    } catch (const std::exception& e) {
        LOG_ERROR("fatal error: " + std::string(e.what()));
        return 1;
    }
}