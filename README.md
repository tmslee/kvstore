# KVStore
A high-performance key-value store written in C++20, featuring both in-memory and disk-based storage, TCP networking with text and binary protocols, TTL support, and comprehensive persistence.

## Features
- **Multiple Storage Backends**
  - In-memory store with `shared_mutex` for concurrent access
  - Disk-based store with log-structured storage and compaction

- **Persistence**
  - Write-ahead logging (WAL) for durability
  - Snapshots for fast recovery
  - Automatic compaction

- **Networking**
  - TCP server with thread-per-connection model
  - Text protocol (human-readable, telnet-compatible)
  - Binary protocol (length-prefixed, efficient)
  - Auto-detection of protocol type

- **TTL Support**
  - Per-key expiration times
  - Lazy expiration on access
  - TTL persisted across restarts

- **Production Ready**
  - Graceful shutdown with signal handling
  - Configurable via file and CLI
  - Structured logging with levels
  - Connection limits and timeouts

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
./kvstore_client

# With options
./kvstore_client --host 192.168.1.100 --port 6380 --binary --timeout 60

# Example session
$ ./kvstore_client
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
│   │       └── protocol_handler.hpp
│   └── util/
│       ├── types.hpp           # Time types
│       ├── binary_io.hpp       # Binary I/O utilities
│       ├── clock.hpp           # Clock abstraction
│       ├── config.hpp          # Configuration
│       ├── logger.hpp          # Logging
│       └── signal_handler.hpp  # Signal handling
├── src/                        # Implementation files
├── bin/
│   ├── server_main.cpp         # Server executable
│   └── client_main.cpp         # CLI client
├── tests/                      # Unit tests
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
put (key=16, val=64)         100000 ops  elapsed time=0.08 s  throughput=1257229 ops/s  avg latency=0.80 us
put (key=16, val=1024)       100000 ops  elapsed time=0.13 s  throughput=760142 ops/s  avg latency=1.32 us
get                          100000 ops  elapsed time=0.05 s  throughput=1841291 ops/s  avg latency=0.54 us
mixed (80% reads)            100000 ops  elapsed time=0.08 s  throughput=1291920 ops/s  avg latency=0.77 us

--- DiskStore ---
put (key=16, val=64)          10000 ops  elapsed time=0.02 s  throughput=424410 ops/s  avg latency=2.36 us
put (key=16, val=1024)        10000 ops  elapsed time=0.03 s  throughput=309357 ops/s  avg latency=3.23 us
get                           10000 ops  elapsed time=0.02 s  throughput=601649 ops/s  avg latency=1.66 us
mixed (80% reads)             10000 ops  elapsed time=0.02 s  throughput=530645 ops/s  avg latency=1.88 us

2026-01-28 12:54:19.561 [INFO ] Server started on 127.0.0.1:0
--- Network throughput (text) ---
ping                         100000 ops  elapsed time=2.06 s  throughput=48642 ops/s  avg latency=20.56 us
put (key=16, val=64)         100000 ops  elapsed time=2.04 s  throughput=49088 ops/s  avg latency=20.37 us
put (key=16, val=1024)       100000 ops  elapsed time=2.19 s  throughput=45723 ops/s  avg latency=21.87 us
get                          100000 ops  elapsed time=2.38 s  throughput=41954 ops/s  avg latency=23.84 us

--- Network latency (text) ---
ping                       p50=19.41 us  p90=21.49 us  p99=37.43 us  p99.9=154.80 us  max=273.95 us
put (key=16, val=64)       p50=18.50 us  p90=19.70 us  p99=27.03 us  p99.9=171.19 us  max=1589.87 us
put (key=16, val=1024)     p50=20.35 us  p90=21.13 us  p99=26.31 us  p99.9=43.26 us  max=10662.67 us
get                        p50=23.21 us  p90=24.15 us  p99=41.62 us  p99.9=101.18 us  max=450.28 us

--- Multi-threaded (text) ---
put (key=16, val=64)       threads=1  ops=10000  time=0.19 s  throughput=51569 ops/s
put (key=16, val=64)       threads=2  ops=20000  time=0.21 s  throughput=94566 ops/s
put (key=16, val=64)       threads=4  ops=40000  time=0.21 s  throughput=189264 ops/s
put (key=16, val=64)       threads=8  ops=80000  time=0.23 s  throughput=340575 ops/s

mixed (80% reads)          threads=1  ops=10000  time=0.24 s  throughput=42072 ops/s
mixed (80% reads)          threads=2  ops=20000  time=0.24 s  throughput=82303 ops/s
mixed (80% reads)          threads=4  ops=40000  time=0.26 s  throughput=156723 ops/s
mixed (80% reads)          threads=8  ops=80000  time=0.29 s  throughput=275685 ops/s

--- Protocol comparison ---
text: put (key=16, val=64)     50000 ops  elapsed time=0.95 s  throughput=52367 ops/s  avg latency=19.10 us
binary: put (key=16, val=64)     50000 ops  elapsed time=0.92 s  throughput=54440 ops/s  avg latency=18.37 us

2026-01-28 12:54:35.491 [INFO ] Server stopping...
2026-01-28 12:54:35.491 [INFO ] Server stopped
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
