# KVStore
A key-value store written in C++20, featuring both in-memory and disk-based storage, event-driven TCP networking with text and binary protocols, TTL support, and comprehensive persistence.

## Features
- **Multiple Storage Backends**
  - In-memory store with `shared_mutex` for concurrent read access
  - Disk-based store with log-structured storage and compaction

- **Persistence**
  - Write-ahead logging (WAL) for durability
  - Snapshots for fast recovery
  - Automatic compaction

- **Networking**
  - Single-threaded event loop with epoll (Linux) / kqueue (macOS/BSD)
  - Non-blocking I/O with connection buffering
  - Text protocol (human-readable, telnet-compatible)
  - Binary protocol (length-prefixed, efficient)
  - Auto-detection of protocol type
  - TCP_NODELAY for low latency

- **TTL Support**
  - Per-key expiration times
  - Lazy expiration on access + background cleanup
  - TTL persisted across restarts

- **Production Ready**
  - Graceful shutdown with async-signal-safe handling
  - Configurable via file and CLI
  - Structured logging with levels
  - Connection limits and timeouts
  - RAII resource management throughout

## Building
### Requirements
- C++20 compatible compiler
- CMake 3.20+
- Google Test (fetched automatically)

### Build
```bash
mkdir build && cd build
cmake ..
make
```

### Build with sanitizers
```bash
# Address Sanitizer
cmake .. -DENABLE_ASAN=ON
make

# Thread Sanitizer
cmake .. -DENABLE_TSAN=ON
make

# Undefined Behavior Sanitizer
cmake .. -DENABLE_UBSAN=ON
make
```

### Run tests
```bash
cd build
ctest --output-on-failure
```

## Usage
### Starting the server
```bash
# Default settings
./kvstore-server

# With config file
./kvstore-server --config /path/to/kvstore.conf

# With CLI options
./kvstore-server --host 0.0.0.0 --port 6379 --data-dir /var/lib/kvstore

# All options
./kvstore-server --help
```

### Using the CLI client
```bash
# Start the client
./kvstore-client

# With options
./kvstore-client --host 192.168.1.100 --port 6380 --binary --timeout 60

# Example session
$ ./kvstore-client
Connected to 127.0.0.1:6379
> PUT name Alice
OK
> GET name
OK Alice
> PUTEX session 60000 abc123
OK
> EXISTS session
OK 1
> SIZE
OK 2
> DEL name
OK
> PING
OK PONG
> HELP
Commands: PUT, PUTEX, GET, DEL, EXISTS, SIZE, CLEAR, PING, QUIT
> QUIT
BYE
```

### Configuration
Create a `kvstore.conf` file:
```toml
# Server settings
host = 127.0.0.1
port = 6379
max_connections = 1000
client_timeout_seconds = 300

# Storage settings
data_dir = /var/lib/kvstore
snapshot_threshold = 10000
compaction_threshold = 100000
use_disk_store = false

# Logging
log_level = info
```

CLI options override config file settings.

### Connecting with telnet (text protocol)
```bash
$ telnet localhost 6379
PUT mykey myvalue
OK
GET mykey
OK myvalue
DEL mykey
OK
QUIT
BYE
```

### Text protocol commands

| Command              | Description                   | Example                      |
|----------------------|-------------------------------|------------------------------|
| `PUT key value`      | Store a value                 | `PUT name Alice`             |
| `PUTEX key ms value` | Store with TTL (milliseconds) | `PUTEX session 60000 abc123` |
| `GET key`            | Retrieve a value              | `GET name`                   |
| `DEL key`            | Delete a key                  | `DEL name`                   |
| `EXISTS key`         | Check if key exists           | `EXISTS name`                |
| `SIZE`               | Get number of keys            | `SIZE`                       |
| `CLEAR`              | Delete all keys               | `CLEAR`                      |
| `PING`               | Health check                  | `PING`                       |
| `QUIT`               | Close connection              | `QUIT`                       |

Command aliases: `SET`=`PUT`, `SETEX`=`PUTEX`, `DELETE`/`REMOVE`=`DEL`, `CONTAINS`=`EXISTS`, `COUNT`=`SIZE`, `EXIT`=`QUIT`

### Using the client library
```cpp
#include "kvstore/net/client/client.hpp"

using namespace kvstore::net::client;

int main() {
    ClientOptions opts;
    opts.host = "127.0.0.1";
    opts.port = 6379;
    opts.binary = true;  // Use binary protocol

    Client client(opts);
    client.connect();

    // Basic operations
    client.put("name", "Alice");
    
    auto value = client.get("name");
    if (value) {
        std::cout << "name = " << *value << std::endl;
    }

    // With TTL (expires in 60 seconds)
    client.put("session", "abc123", std::chrono::milliseconds(60000));

    // Other operations
    bool exists = client.contains("name");
    size_t count = client.size();
    bool removed = client.remove("name");
    client.clear();

    // Health check
    if (client.ping()) {
        std::cout << "Server is up" << std::endl;
    }

    client.disconnect();
    return 0;
}
```

### Using the store directly (embedded)
```cpp
#include "kvstore/core/store.hpp"

using namespace kvstore::core;

int main() {
    // In-memory store with WAL
    StoreOptions opts;
    opts.data_dir = "/var/lib/kvstore";
    opts.snapshot_threshold = 10000;

    Store store(opts);

    // Basic operations
    store.put("key1", "value1");
    
    auto value = store.get("key1");
    if (value) {
        std::cout << *value << std::endl;
    }

    // With TTL
    store.put("temp", "data", std::chrono::milliseconds(5000));

    // Persistence
    store.snapshot();  // Force snapshot

    return 0;
}
```

### Using disk-based storage
```cpp
#include "kvstore/core/disk_store.hpp"

using namespace kvstore::core;

int main() {
    DiskStoreOptions opts;
    opts.data_dir = "/var/lib/kvstore";
    opts.compaction_threshold = 100000;

    DiskStore store(opts);

    store.put("key1", "value1");
    auto value = store.get("key1");

    store.compact();  // Force compaction

    return 0;
}
```

## Binary Protocol
The binary protocol uses length-prefixed messages for efficiency:
```
Message: [4 bytes: length (big-endian)][payload]
Request: [1 byte: command][command-specific data]
Response: [1 byte: status][optional data]
```

### Commands (uint8)
| Value | Command |
|-------|---------|
| 1     | GET     |
| 2     | PUT     |
| 3     | PUTEX   |
| 4     | DEL     |
| 5     | EXISTS  |
| 6     | SIZE    |
| 7     | CLEAR   |
| 8     | PING    |
| 9     | QUIT    |

### Status (uint8)
| Value | Status    |
|-------|-----------|
| 0     | OK        |
| 1     | NOT_FOUND |
| 2     | ERROR     |
| 3     | BYE       |

Strings are length-prefixed: `[4 bytes: length][data]`

## Architecture
```
┌─────────────────────────────────────────────────────────────┐
│                         Client                              │
│  ┌─────────────────┐  ┌─────────────────┐                   │
│  │ TextProtocol    │  │ BinaryProtocol  │                   │
│  └────────┬────────┘  └────────┬────────┘                   │
│           └──────────┬─────────┘                            │
│                      ▼                                      │
│              ProtocolHandler                                │
└──────────────────────┬──────────────────────────────────────┘
                       │ TCP
┌──────────────────────┴──────────────────────────────────────┐
│                        Server                               │
│              ProtocolHandler                                │
│           ┌──────────┴─────────┐                            │
│  ┌────────┴────────┐  ┌────────┴────────┐                   │
│  │ TextProtocol    │  │ BinaryProtocol  │                   │
│  └─────────────────┘  └─────────────────┘                   │
│                      │                                      │
│                      ▼                                      │
│              ┌───────────────┐                              │
│              │    IStore     │                              │
│              └───────┬───────┘                              │
│           ┌──────────┴──────────┐                           │
│           ▼                     ▼                           │
│    ┌─────────────┐      ┌─────────────┐                     │
│    │    Store    │      │  DiskStore  │                     │
│    │ (in-memory) │      │(log-struct) │                     │
│    └──────┬──────┘      └─────────────┘                     │
│           │                                                 │
│     ┌─────┴─────┐                                           │
│     ▼           ▼                                           │
│  ┌─────┐   ┌──────────┐                                     │
│  │ WAL │   │ Snapshot │                                     │
│  └─────┘   └──────────┘                                     │
└─────────────────────────────────────────────────────────────┘
```

## Project Structure
```
├── include/kvstore/
│   ├── core/
│   │   ├── istore.hpp          # Storage interface
│   │   ├── store.hpp           # In-memory store
│   │   ├── disk_store.hpp      # Disk-based store
│   │   ├── wal.hpp             # Write-ahead log
│   │   └── snapshot.hpp        # Snapshot persistence
│   ├── net/
│   │   ├── types.hpp           # Protocol types (Command, Status, Request, Response)
│   │   ├── binary_protocol.hpp # Binary encode/decode
│   │   ├── text_protocol.hpp   # Text encode/decode
│   │   ├── client/
│   │   │   ├── client.hpp      # Client class
│   │   │   └── protocol_handler.hpp
│   │   └── server/
│   │       ├── server.hpp      # Server class
│   │       ├── protocol_handler.hpp
│   │       ├── connection.hpp  # Per-client connection state
│   │       └── event_loop.hpp  # Event loop (epoll/kqueue)
│   └── util/
│       ├── types.hpp           # Time types
│       ├── binary_io.hpp       # Binary I/O utilities
│       ├── clock.hpp           # Clock abstraction
│       ├── config.hpp          # Configuration
│       ├── fd_guard.hpp        # RAII file descriptor wrapper
│       ├── logger.hpp          # Logging
│       └── signal_handler.hpp  # Signal handling
├── src/                        # Implementation files
├── bin/
│   ├── server_main.cpp         # Server executable
│   └── client_main.cpp         # CLI client
├── tests/
│   ├── core/                   # Store, WAL, snapshot, disk_store tests
│   ├── net/                    # Protocol, client, server tests
│   └── util/                   # Config, logger, signal handler tests
├── bench/
│   ├── benchmark.hpp           # Benchmark utilities
│   └── benchmark.cpp           # Benchmark suite
└── CMakeLists.txt
```

## Benchmarks
Run benchmarks:
```bash
cd build
./kvstore-benchmark

# Options
./kvstore-benchmark --ops 500000      # More operations
./kvstore-benchmark --binary          # Use binary protocol
./kvstore-benchmark --no-network      # Skip network tests
./kvstore-benchmark --no-disk         # Skip disk tests
./kvstore-benchmark --help            # All options
```

Sample results (localhost, single-threaded):
```
=== KVstore Benchmark ===
Operations per tests: 100000

--- Store (in-memory) ---
put (key=16, val=64)         100000 ops  elapsed time=0.44 s  throughput=225517 ops/s  avg latency=4.43 us
put (key=16, val=1024)       100000 ops  elapsed time=0.83 s  throughput=121123 ops/s  avg latency=8.26 us
get                          100000 ops  elapsed time=0.36 s  throughput=279628 ops/s  avg latency=3.58 us
mixed (80% reads)            100000 ops  elapsed time=0.44 s  throughput=228742 ops/s  avg latency=4.37 us

--- DiskStore ---
put (key=16, val=64)          10000 ops  elapsed time=0.10 s  throughput=104108 ops/s  avg latency=9.61 us
put (key=16, val=1024)        10000 ops  elapsed time=0.12 s  throughput=84186 ops/s  avg latency=11.88 us
get                           10000 ops  elapsed time=0.08 s  throughput=129597 ops/s  avg latency=7.72 us
mixed (80% reads)             10000 ops  elapsed time=0.09 s  throughput=112133 ops/s  avg latency=8.92 us

--- Network throughput (text) ---
ping                         100000 ops  elapsed time=4.58 s  throughput=21824 ops/s  avg latency=45.82 us
put (key=16, val=64)         100000 ops  elapsed time=6.10 s  throughput=16402 ops/s  avg latency=60.97 us
put (key=16, val=1024)       100000 ops  elapsed time=17.82 s  throughput=5610 ops/s  avg latency=178.24 us
get                          100000 ops  elapsed time=5.64 s  throughput=17719 ops/s  avg latency=56.44 us

--- Network latency (text) ---
ping                       p50=43.70 us  p90=47.37 us  p99=83.62 us  p99.9=178.10 us  max=193.77 us
put (key=16, val=64)       p50=62.28 us  p90=70.56 us  p99=129.33 us  p99.9=216.01 us  max=263.10 us
put (key=16, val=1024)     p50=174.28 us  p90=191.13 us  p99=287.78 us  p99.9=468.00 us  max=543.80 us
get                        p50=53.67 us  p90=62.33 us  p99=131.71 us  p99.9=228.38 us  max=297.61 us

--- Multi-threaded (text) ---
put (key=16, val=64)       threads=1  ops=10000  time=0.63 s  throughput=15990 ops/s
put (key=16, val=64)       threads=2  ops=20000  time=0.74 s  throughput=27137 ops/s
put (key=16, val=64)       threads=4  ops=40000  time=1.46 s  throughput=27480 ops/s
put (key=16, val=64)       threads=8  ops=80000  time=2.86 s  throughput=27942 ops/s

mixed (80% reads)          threads=1  ops=10000  time=0.61 s  throughput=16320 ops/s
mixed (80% reads)          threads=2  ops=20000  time=0.60 s  throughput=33178 ops/s
mixed (80% reads)          threads=4  ops=40000  time=1.09 s  throughput=36710 ops/s
mixed (80% reads)          threads=8  ops=80000  time=2.18 s  throughput=36636 ops/s

--- Protocol comparison ---
text: put (key=16, val=64)     50000 ops  elapsed time=3.10 s  throughput=16106 ops/s  avg latency=62.09 us
binary: put (key=16, val=64)     50000 ops  elapsed time=2.49 s  throughput=20102 ops/s  avg latency=49.75 us

Benchmark complete
```

## Testing
```bash
cd build
ctest --output-on-failure

# Run specific test
./tests/store_test
./tests/client_test

# With verbose output
./tests/store_test --gtest_filter="*TTL*"
```

## License
MIT License
