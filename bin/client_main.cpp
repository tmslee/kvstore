#include "kvstore/net/client/client.hpp"

#include <iostream>
#include <sstream>
#include <string>

using namespace kvstore::net::client;

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  --host HOST       Server host (default: 127.0.0.1)\n"
              << "  --port PORT       Server port (default: 6379)\n"
              << "  --binary          Use binary protocol\n"
              << "  --timeout SECS    Connection timeout (default: 30)\n"
              << "  --help            Show this help\n"
              << "\n"
              << "Commands:\n"
              << "  PUT key value     Store a value\n"
              << "  PUTEX key ms val  Store with TTL (milliseconds)\n"
              << "  GET key           Retrieve a value\n"
              << "  DEL key           Delete a key\n"
              << "  EXISTS key        Check if key exists\n"
              << "  SIZE              Get number of keys\n"
              << "  CLEAR             Delete all keys\n"
              << "  PING              Health check\n"
              << "  QUIT              Exit client\n";
}

int main(int argc, char* argv[]) {
    ClientOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            opts.host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            opts.port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--binary") {
            opts.binary = true;
        } else if (arg == "--timeout" && i + 1 < argc) {
            opts.timeout_seconds = std::stoi(argv[++i]);
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    Client client(opts);

    try {
        client.connect();
        std::cout << "Connected to " << opts.host << ":" << opts.port;
        if (opts.binary) {
            std::cout << " (binary)";
        }
        std::cout << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Connection failed: " << e.what() << std::endl;
        return 1;
    }

    std::string line;
    std::cout << "> ";

    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            std::cout << "> ";
            continue;
        }

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        // Convert to uppercase
        for (char& c : cmd) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        try {
            if (cmd == "PUT" || cmd == "SET") {
                std::string key;
                iss >> key;
                std::string value;
                std::getline(iss >> std::ws, value);

                if (key.empty() || value.empty()) {
                    std::cout << "ERROR usage: PUT key value" << std::endl;
                } else {
                    client.put(key, value);
                    std::cout << "OK" << std::endl;
                }

            } else if (cmd == "PUTEX" || cmd == "SETEX") {
                std::string key;
                int64_t ttl_ms;
                iss >> key >> ttl_ms;
                std::string value;
                std::getline(iss >> std::ws, value);

                if (key.empty() || value.empty()) {
                    std::cout << "ERROR usage: PUTEX key ms value" << std::endl;
                } else {
                    client.put(key, value, kvstore::util::Duration(ttl_ms));
                    std::cout << "OK" << std::endl;
                }

            } else if (cmd == "GET") {
                std::string key;
                iss >> key;

                if (key.empty()) {
                    std::cout << "ERROR usage: GET key" << std::endl;
                } else {
                    auto value = client.get(key);
                    if (value) {
                        std::cout << "OK " << *value << std::endl;
                    } else {
                        std::cout << "NOT_FOUND" << std::endl;
                    }
                }

            } else if (cmd == "DEL" || cmd == "DELETE" || cmd == "REMOVE") {
                std::string key;
                iss >> key;

                if (key.empty()) {
                    std::cout << "ERROR usage: DEL key" << std::endl;
                } else {
                    if (client.remove(key)) {
                        std::cout << "OK" << std::endl;
                    } else {
                        std::cout << "NOT_FOUND" << std::endl;
                    }
                }

            } else if (cmd == "EXISTS" || cmd == "CONTAINS") {
                std::string key;
                iss >> key;

                if (key.empty()) {
                    std::cout << "ERROR usage: EXISTS key" << std::endl;
                } else {
                    std::cout << "OK " << (client.contains(key) ? "1" : "0") << std::endl;
                }

            } else if (cmd == "SIZE" || cmd == "COUNT") {
                std::cout << "OK " << client.size() << std::endl;

            } else if (cmd == "CLEAR") {
                client.clear();
                std::cout << "OK" << std::endl;

            } else if (cmd == "PING") {
                if (client.ping()) {
                    std::cout << "OK PONG" << std::endl;
                } else {
                    std::cout << "ERROR ping failed" << std::endl;
                }

            } else if (cmd == "QUIT" || cmd == "EXIT") {
                std::cout << "BYE" << std::endl;
                break;

            } else if (cmd == "HELP") {
                std::cout << "Commands: PUT, PUTEX, GET, DEL, EXISTS, SIZE, CLEAR, PING, QUIT" << std::endl;

            } else {
                std::cout << "ERROR unknown command: " << cmd << std::endl;
            }

        } catch (const std::exception& e) {
            std::cout << "ERROR " << e.what() << std::endl;

            // Try to reconnect
            if (!client.connected()) {
                try {
                    client.connect();
                    std::cout << "Reconnected" << std::endl;
                } catch (...) {
                    std::cerr << "Reconnection failed, exiting" << std::endl;
                    return 1;
                }
            }
        }

        std::cout << "> ";
    }

    client.disconnect();
    return 0;
}