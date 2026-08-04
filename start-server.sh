#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GEN_TASKS="$SCRIPT_DIR/build/common/gen-tasks"
AREG_SERVER="$SCRIPT_DIR/build/bin/areg_server.elf"
MTROUTER="$SCRIPT_DIR/build/bin/mtrouter.elf"
GRPC_SERVER="$SCRIPT_DIR/build/grpc-src/grpc-server"
CONFIG_TEMPLATE="$SCRIPT_DIR/build/bin/config/areg.init"
WORK_CONFIG="$CONFIG_TEMPLATE"

# ── defaults ──────────────────────────────────────────────────
TRANSPORT="areg"
TASK_FILE="tasks.txt"
OUT_FILE="out.csv"
NUM_SAMPLES=""
DIM_LOWER="1"
DIM_UPPER="10"
BIND_ADDRESS="localhost:50000"     

# ── helpers ────────────────────────────────────────────────────
usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Start a gRPC or AREG task-distribution server.  Tasks can be generated
on-the-fly with the bundled gen-tasks utility, or you can supply a
pre-existing task file.

Options:
  -t, --transport TRANSPORT  Server transport: "areg" (default) or "grpc"
  -n, --num-samples N        Number of tasks to generate
      --dim-lower N          Lower bound for matrix dimensions
      --dim-upper N          Upper bound for matrix dimensions
  -f, --task-file FILE       Task file path
  -a, --address ADDR         Bind / listen address (host:port)
  -o, --out-file FILE        name of a file to record the statistics
  -h, --help                 Show this help message

Examples:
  # Generate 100 tasks (dim 5‑20) and serve with AREG
  $0 -n 100 --dim-lower 5 --dim-upper 20

  # Use an existing task file with gRPC on a custom port
  $0 -t grpc -f my_tasks.txt -a 0.0.0.0:9999

  # Generate tasks, write to a named file, serve with gRPC
  $0 -t grpc -n 50 --dim-lower 10 --dim-upper 50 -f bench.txt

  # AREG with a custom router address
  $0 -n 200 --dim-lower 5 --dim-upper 20 -a 192.168.1.10:8181
EOF
  exit 0
}

die() { echo "ERROR: $*" >&2; exit 1; }

cleanup() {
  if [[ -n "$MTROUTER_PID" ]] && kill -0 "$MTROUTER_PID" 2>/dev/null; then
    echo "Stopping mtrouter (pid $MTROUTER_PID) …"
    kill "$MTROUTER_PID" 2>/dev/null || true
    wait "$MTROUTER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

# ── parse arguments ────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
  case "$1" in
    -t|--transport)   TRANSPORT="$2"; shift 2 ;;
    -n|--num-samples) NUM_SAMPLES="$2"; shift 2 ;;
    --dim-lower)      DIM_LOWER="$2";   shift 2 ;;
    --dim-upper)      DIM_UPPER="$2";   shift 2 ;;
    -f|--task-file)   TASK_FILE="$2";   shift 2 ;;
    -a|--address)     BIND_ADDRESS="$2"; shift 2 ;;
    -o|--out-file)    OUT_FILE="$2";    shift 2 ;;
    -h|--help)        usage ;;
    *) die "Unknown option: $1" ;;
  esac
done

# ── validate transport ─────────────────────────────────────────
TRANSPORT="${TRANSPORT,,}"          # lowercase
if [[ "$TRANSPORT" != "areg" && "$TRANSPORT" != "grpc" ]]; then
  die "Invalid transport '$TRANSPORT' — must be 'areg' or 'grpc'"
fi

# ── resolve task file ──────────────────────────────────────────
if [[ -n "$NUM_SAMPLES" ]]; then
  echo "Generating $NUM_SAMPLES tasks (dim $DIM_LOWER‑$DIM_UPPER) → $TASK_FILE"
  "$GEN_TASKS" "$NUM_SAMPLES" "$DIM_LOWER" "$DIM_UPPER" "$TASK_FILE"
  echo "Done."
else
  echo "Using existing task file: $TASK_FILE"
fi

# ── launch server ──────────────────────────────────────────────
if [[ "$TRANSPORT" == "areg" ]]; then
  # ── AREG: prepare config, start mtrouter, then start server ──

  BIND_ADDRESS="${BIND_ADDRESS:-localhost:8181}"
  AREG_HOST="${BIND_ADDRESS%:*}"
  AREG_PORT="${BIND_ADDRESS##*:}"

  # Write working config from the full template, patching router address.
  sed -e "s/^router::\*::address::tcpip\s*=.*/router::*::address::tcpip   = $AREG_HOST/" \
      -e "s/^router::\*::port::tcpip\s*=.*/router::*::port::tcpip      = $AREG_PORT/" \
      "$CONFIG_TEMPLATE" > "$WORK_CONFIG"

  echo "Router config written to $WORK_CONFIG  ($AREG_HOST:$AREG_PORT)"

  # Start the message router in the background.
  echo "Starting mtrouter …"
  "$MTROUTER" --load="$WORK_CONFIG" -t --service &
  MTROUTER_PID=$!

  # Give the router a moment to bind.
  sleep 1

  if ! kill -0 "$MTROUTER_PID" 2>/dev/null; then
    die "mtrouter failed to start"
  fi

  echo "mtrouter running (pid $MTROUTER_PID)"

  # Launch the AREG server.  It reads areg.init from its CWD,
  # so we cd to the project root where we just wrote the config.
  echo "Starting AREG server with $TASK_FILE …"
  cd "$SCRIPT_DIR"
  exec "$AREG_SERVER" "$TASK_FILE" "$OUT_FILE"

else
  # ── gRPC: start server directly ──────────────────────────────

  echo "Starting gRPC server on $BIND_ADDRESS with $TASK_FILE …"
  exec "$GRPC_SERVER" "$TASK_FILE" "$BIND_ADDRESS" "$OUT_FILE"

fi
