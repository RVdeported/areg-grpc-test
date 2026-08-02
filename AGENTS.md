# areg-backtest

C++20 benchmark comparing **AREG SDK** (service-oriented RPC) vs **gRPC** (bidi streaming) for distributed matrix-multiplication task processing. A master dispatches tasks to workers; both transports share `common/utils.hpp` so transport is the only variable.

## Dev environment

- **Compiler**: GCC 15 (`/usr/bin/g++`), **CMake ≥ 3.23**
- **Language**: C++20 (`CMAKE_CXX_STANDARD 20`) — but code uses C++23 library features (`<print>`, `std::views::slide`); GCC 15 permits them with `-std=c++20`
- **Dependencies**:
  - **AREG SDK**: auto-fetched from GitHub (`https://github.com/aregtech/areg-sdk.git`, branch `master`) via FetchContent if not installed as a system package. Requires network on first build.
  - **gRPC + Protobuf**: must be pre-installed. In this repo's setup, found at `/home/ugrek/anaconda3/lib/cmake/grpc`.
- All generated code lives under `build/` — never hand-edit files in `build/generate/` or `build/grpc-src/generated/`.

## Build

Out-of-source build in `build/`. The directory already exists and is pre-configured.

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug    # or Release
make -j$(nproc)
```

Build artifacts:
- `build/common/gen-tasks` — task generator tool
- `build/bin/areg_server.elf`, `build/bin/areg_client.elf` — AREG master/worker
- `build/grpc-src/grpc-server`, `build/grpc-src/grpc-client` — gRPC master/worker

## Run

Generate tasks first:
```bash
./build/common/gen-tasks 100 5 20 tmp.txt
# Usage: gen-tasks <num_samples> <dim_lower> <dim_upper> [output_file]
# Dimensions capped at 100. Matrix values ~ N(0,1).
```

**AREG SDK** (request/response pattern):
```bash
./build/bin/areg_server.elf tmp.txt &
./build/bin/areg_client.elf
```

**gRPC** (bidi streaming):
```bash
# Server
./build/grpc-src/grpc-server tmp.txt [bind_address]
# Client(s)
./build/grpc-src/grpc-client [server_address]
```

Timing output is written to `out.csv` in the working directory on server shutdown.

## Code conventions

- **Formatting**: `.clang-format` at project root — Allman braces, 80-col limit, 2-space indent, pointer alignment middle (`Type * ptr`), sorted includes. Run `clang-format -i <file>` before committing.
- **Naming**: PascalCase classes (`MasterComponent`), camelCase members (`mWorkerId`), snake_case free functions (`read_tasks`). Type aliases `I = uint32_t`, `F = double` in `common::`.
- **Headers**: C++ standard library headers (`<print>`, `<vector>`) then third-party (`<grpcpp/grpcpp.h>`) then project headers with paths relative to repo root (`"common/utils.hpp"`, `"areg-src/Interface.hpp"`).
- **Timestamps**: microseconds since epoch via `common::ts()` → `size_t`.
- **No tests, no lint** — clang-format is the only code-quality tool configured.

## Pitfalls

- **`g_output_dir` must be set before framework init** in AREG server — AREG changes the working directory, so capture `std::filesystem::current_path()` early or `out.csv` ends up in the wrong location.
- **AREG code-gen leaves stale files** in `build/generate/` if `Interface.siml` changes — delete the directory and reconfigure CMake to regenerate cleanly.
- **gRPC bidi streams have no idle timeout** — a crashed worker with an open stream won't be detected until the next `Read()`/`Write()`; on a task queue drained of tasks, the server may hang indefinitely waiting on a zombie stream.
- **Matrix multiply is deliberately naive** (no BLAS/blocking) — don't "optimize" it; the benchmark measures transport overhead, not compute speed.
- **`.gitignore` excludes `*.txt`, `*.csv`** — generated task files and benchmark output won't be committed.
- **`QWEN.md` is gitignored and stale** — it claims C++23 but CMakeLists.txt says C++20. The CMakeLists is authoritative.
