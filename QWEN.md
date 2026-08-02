# areg-backtest

A C++23 backtesting/benchmarking project comparing two IPC transport mechanisms for distributed matrix-multiplication task processing: **AREG SDK** (service-oriented RPC framework) and **gRPC** (bidi streaming).

## Project Structure

```
areg-backtest/
├── common/                  # Shared utilities (both implementations)
│   ├── utils.hpp            # Task parsing, matrix multiply, CSV recording
│   └── gen_tasks.cpp        # Task generator tool (random matrix-multiply tasks)
├── areg-src/                # AREG SDK implementation
│   ├── Interface.siml       # Service interface definition (AREG code-gen input)
│   ├── server.cpp           # Master (server) entry point
│   ├── client.cpp           # Worker (client) entry point
│   ├── common/
│   │   ├── MasterComponent.{hpp,cpp}   # Task dispatcher & worker registry
│   │   └── WorkerComponent.{hpp,cpp}   # Worker: registers, computes, submits
│   └── private/             # Auto-generated stubs/proxies/events from Interface.siml
├── grpc-src/                # gRPC implementation
│   ├── proto/task.proto     # Protobuf service definition
│   ├── server.cpp           # gRPC server with bidi streaming dispatcher
│   └── client.cpp           # gRPC worker client
└── build/                   # CMake out-of-source build directory
```

## Architecture

Both implementations solve the same problem: a **master** loads matrix-multiplication tasks from a file, workers register and receive tasks, compute `A(n×m) × B(m×k)`, and return results. Timing data is recorded to `out.csv` for benchmarking.

### AREG SDK path

Uses the AREG framework's code generator (`Interface.siml` → generated stubs/proxies in `areg-src/private/`). Communication model:

1. `MasterComponent` implements the service provider (loads tasks from `tmp.txt`, dispatches on request)
2. `WorkerComponent` implements the consumer (auto-registers on connect, computes, submits result)
3. Request/response pattern: `RegisterWorker` → `AssignTaskReply` → `SubmitTask` → (next task)

Build artifacts: `build/bin/areg_server.elf`, `build/bin/areg_client.elf`

### gRPC path

Uses bidi streaming (`stream TaskResult` → `stream ArrayMultiplyTask`) defined in `task.proto`:

1. `RegisterWorker` (unary) — assigns worker IDs
2. `AssignTasks` (bidi stream) — worker sends initial message with its ID, then server writes tasks and reads results inline on the same stream
3. Crash detection is inherent: broken TCP pipe causes `Read()`/`Write()` to fail, no explicit timeout needed

Build artifacts: `build/grpc-src/grpc-server`, `build/grpc-src/grpc-client`

### Shared utilities (`common/`)

- **`Task`** struct: matrix dimensions (`n`, `m`, `k`) and flat row-major arrays `a`, `b`
- **`read_tasks()`**: parses pipe-delimited task files (`n|m|k|a1,a2,...,a_N|b1,b2,...,b_M`)
- **`multiply()`**: naive triple-nested-loop matrix multiply — deliberately unoptimized to keep transport overhead as the primary variable
- **`gen_tasks`**: standalone CLI to generate random task files (`gen_tasks <num> <dim_lower> <dim_upper> [output]`)
- **`TaskRecord`**: captures round-trip timestamps (send → worker recv → worker compute → worker send → server recv)
- **`record_csv()`**: writes all records to `out.csv`

## Build System

- **CMake** (minimum 3.23), out-of-source build in `build/`
- **C++23** (`CMAKE_CXX_STANDARD 23`), GCC 15 (`/usr/local/bin/c++`)
- **AREG SDK** (must be installed on system, found via CMake `find_package`)
- **gRPC** (from `/home/ugrek/anaconda3/lib/cmake/grpc`)

### Build

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug    # or Release
make -j$(nproc)
```

### Run (AREG SDK)

```bash
# Generate tasks first
./build/common/gen-tasks 100 5 20 tmp.txt

# Start master (reads tmp.txt)
./build/bin/areg_server.elf tmp.txt &

# Start worker(s)
./build/bin/areg_client.elf
```

### Run (gRPC)

```bash
# Server
./build/grpc-src/grpc-server tmp.txt [bind_address]

# Client(s)
./build/grpc-src/grpc-client [server_address]
```

Output timings are written to `out.csv` on server shutdown.

### Task generation

```bash
./build/common/gen-tasks <num_samples> <dim_lower> <dim_upper> [output_file]
# Example: 100 tasks with matrices between 5×M and 20×M
./build/common/gen-tasks 100 5 20 tasks.txt
```

Dimensions are capped at 100. Matrix values are sampled from N(0,1).

## Code Style

- **clang-format**: `.clang-format` at project root — Allman brace style, 80-char column limit, 2-space indent, pointer alignment middle (`Type * ptr`), reflow comments
- **Naming**: PascalCase for classes (`MasterComponent`), camelCase for members (`mWorkerId`), snake_case for functions (`read_tasks`)
- **Type aliases** in `common/`: `I` = `uint32_t`, `F` = `double`
- **Timestamps**: microseconds since epoch via `common::ts()` → `size_t`

## Key Design Decisions

- Matrix multiply is intentionally naive (no BLAS, no blocking) — the goal is to measure transport overhead, not compute speed
- The gRPC bidi streaming design was chosen over separate unary endpoints for better crash detection on long-running tasks (no explicit idle/busy timeout needed)
- Both implementations share `common/utils.hpp` so transport is the only variable being benchmarked
- Generated AREG code lives in `areg-src/private/` (produced by the AREG code generator from `Interface.siml`)

## Ignored Files

`.gitignore` excludes: `build/`, `*.json`, `*.txt`, `*.csv`, `.cache`, `QWEN.md`
