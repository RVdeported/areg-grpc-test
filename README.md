# areg-backtest

C++20 benchmark comparing **AREG SDK** (service-oriented RPC) vs **gRPC** (bidi
streaming) for distributed matrix-multiplication task processing. A master
dispatches tasks to workers; both transports share `common/utils.hpp`, so the
transport is the only variable.

This benchmark accompanies a **Habr article** — link: `https://habr.com/ru/article/edit/1064204/`

## Running the benchmark

There are two orchestrators, both driven by `benchmark.ini`.

### 1. `docker_bench.py` — Docker-based

Runs everything inside containers via `docker compose`. Generates one shared
task file on the host, then for every `(transport, client_count)` pair runs
`docker compose up` and collects the CSV results from the mounted `./results`
volume.

```bash
python3 docker_bench.py                 # use benchmark.ini
python3 docker_bench.py --dry-run       # print the plan without running
python3 docker_bench.py --no-build      # skip `docker compose build`
python3 docker_bench.py -c my.ini       # custom config
```

Supported transports: `areg`, `grpc`, `areg_many`.

### 2. `run_benchmark.py` — native (host) mode

Delegates server/client lifecycle to the shell scripts `start-server.sh` /
`start-clients.sh`. Supports a local-only run **or** launching clients on a
remote machine over SSH (see the `[remote]` section).

```bash
python3 run_benchmark.py                 # use benchmark.ini
python3 run_benchmark.py --dry-run       # print the plan without running
python3 run_benchmark.py -c my.ini       # custom config
```

Supported transports: `areg`, `grpc`. Requires a prior build (`build/common/gen-tasks`
must exist).

## Deliverables

Build artifacts (produced by `make`):

- `build/common/gen-tasks` — task-file generator (`gen-tasks <samples> <dim_lower> <dim_upper> [out_file]`)
- `build/bin/areg_server.elf` — AREG master (request/response)
- `build/bin/areg_client.elf` — AREG worker
- `build/bin/areg_client_many.elf` — AREG multi-worker client (used by the `areg_many` transport)
- `build/bin/mtrouter.elf` — AREG message router (plus `logobserver`/`logcollector` helpers)
- `build/grpc-src/grpc-server` — gRPC master (bidi streaming)
- `build/grpc-src/grpc-client` — gRPC worker

Runtime output:

- One CSV per run — `<transport>_out_<clients>.csv` — written to `results_dir`
  (or the mounted `./results` volume under Docker) on server shutdown, with
  per-task timing.

## Configuration (`benchmark.ini`)

| Section      | Key            | Meaning |
|--------------|----------------|---------|
| `[paths]`    | `results_dir`  | Where result CSVs are written |
| `[remote]`   | `host`/`user`/`port`/`repo_dir` | SSH target for clients (empty host = local-only; used by `run_benchmark.py`) |
| `[server]`   | `bind_address`, `grpc_port`, `areg_port`, `startup_wait` | Server bind address/ports and startup wait |
| `[benchmark]`| `transports`   | Comma-separated transports to test |
| `[benchmark]`| `client_counts`| Comma-separated worker counts |
| `[benchmark]`| `total_tasks`  | Number of tasks in the shared task file |
| `[benchmark]`| `dim_lower` / `dim_upper` | Matrix dimension bounds (uniform random) |

## Building

Required only for `run_benchmark.py` (and to produce the `gen-tasks` tool).

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```
