#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
AREG_CLIENT="$SCRIPT_DIR/build/bin/areg_client.elf"
GRPC_CLIENT="$SCRIPT_DIR/build/grpc-src/grpc-client"
AREG_CONFIG="$SCRIPT_DIR/areg.init"
AREG_FINAL_CONFIG="$SCRIPT_DIR/build/bin/config/areg.init"

# Seed from the AREG SDK default template on first run.
if [[ ! -f "$AREG_CONFIG" ]]; then
  cp "$AREG_FINAL_CONFIG" "$AREG_CONFIG"
fi

# ── defaults ──────────────────────────────────────────────────
TRANSPORT="areg"
NUM_CLIENTS=1
SERVER_ADDR="localhost:50000"           

PIDS=()

# ── helpers ────────────────────────────────────────────────────
usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Launch worker client(s) that connect to a running task-distribution server.

Options:
  -t, --transport TRANSPORT  Transport: "areg" (default) or "grpc"
  -n, --num-clients N        Number of client instances to launch (default: 1)
  -a, --address ADDR         Server address (gRPC only, default: localhost:50000)
  -h, --help                 Show this help message
EOF
  exit 0
}

die() { echo "ERROR: $*" >&2; exit 1; }

cleanup() {
  if [[ ${#PIDS[@]} -gt 0 ]]; then
    echo "Stopping ${#PIDS[@]} client(s) …"
    for pid in "${PIDS[@]}"; do
      kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
  fi
}
trap cleanup EXIT

# ── parse arguments ────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
  case "$1" in
    -t|--transport)    TRANSPORT="$2";    shift 2 ;;
    -n|--num-clients)  NUM_CLIENTS="$2";  shift 2 ;;
    -a|--address)      SERVER_ADDR="$2";  shift 2 ;;
    -h|--help)         usage ;;
    *) die "Unknown option: $1" ;;
  esac
done

# ── validate ───────────────────────────────────────────────────
TRANSPORT="${TRANSPORT,,}"
if [[ "$TRANSPORT" != "areg" && "$TRANSPORT" != "grpc" ]]; then
  die "Invalid transport '$TRANSPORT' — must be 'areg' or 'grpc'"
fi

if [[ "$NUM_CLIENTS" -lt 1 ]]; then
  die "--num-clients must be at least 1"
fi

# ── launch clients ─────────────────────────────────────────────
if [[ "$TRANSPORT" == "areg" ]]; then
  # AREG clients read areg.init from CWD, so cd to project root.
  cd "$SCRIPT_DIR"
  echo "Launching $NUM_CLIENTS AREG client(s) …"
  
  AREG_HOST="${SERVER_ADDR%:*}"
  AREG_PORT="${SERVER_ADDR##*:}"

  sed -e "s/^router::\*::address::tcpip\s*=.*/router::*::address::tcpip   = $AREG_HOST/" \
      -e "s/^router::\*::port::tcpip\s*=.*/router::*::port::tcpip      = $AREG_PORT/" \
      "$AREG_CONFIG" > "$AREG_FINAL_CONFIG"

  echo "Router config written to $AREG_FINAL_CONFIG  ($AREG_HOST:$AREG_PORT)"


  for ((i = 0; i < NUM_CLIENTS; i++)); do
    "$AREG_CLIENT" &
    PIDS+=($!)
  done

  echo "All $NUM_CLIENTS client(s) running.  Press Ctrl+C to stop."
  wait

else
  echo "Launching $NUM_CLIENTS gRPC client(s) → $SERVER_ADDR …"

  for ((i = 0; i < NUM_CLIENTS; i++)); do
    "$GRPC_CLIENT" "$SERVER_ADDR" &
    PIDS+=($!)
  done

  # Wait for all to finish (server closes streams → clients exit).
  for pid in "${PIDS[@]}"; do
    wait "$pid" || true
  done
  echo "All $NUM_CLIENTS gRPC client(s) finished."
fi
