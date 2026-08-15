#!/usr/bin/env python3
"""Docker-based benchmark orchestrator for AREG vs gRPC.

Generates a shared task file once, then for every (transport, client-count)
pair in the INI config, runs ``docker compose up`` with the appropriate
environment variables.  Results are collected from the mounted ``./results``
volume into the configured output directory.

Usage::

    python3 docker_bench.py                     # uses ./benchmark.ini
    python3 docker_bench.py --config my.ini     # custom config
    python3 docker_bench.py --dry-run           # print what would happen
    python3 docker_bench.py --no-build          # skip docker compose build
"""

from __future__ import annotations

import argparse
import configparser
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


# ── helpers ──────────────────────────────────────────────────────────────────

def _resolve(path_str: str, base: Path) -> Path:
    """Expand ``~`` and resolve relative to *base*."""
    s = os.path.expanduser(path_str)
    p = Path(s)
    return p if p.is_absolute() else (base / p).resolve()


# ── main ─────────────────────────────────────────────────────────────────────

def run_benchmark(config_path: Path, dry_run: bool = False,
                  no_build: bool = False) -> None:
    cfg = configparser.ConfigParser()
    cfg.read(config_path)
    config_dir = config_path.parent.resolve()

    # ---- paths ---------------------------------------------------------------
    repo_dir = _resolve(cfg["paths"]["repo_dir"], config_dir)
    results_dir = _resolve(cfg["paths"].get("results_dir", "results"), repo_dir)
    results_dir.mkdir(parents=True, exist_ok=True)

    tasks_dir = repo_dir / "tasks"
    tasks_dir.mkdir(parents=True, exist_ok=True)

    gen_tasks = repo_dir / "build" / "common" / "gen-tasks"
    if not gen_tasks.exists():
        raise SystemExit(f"Task generator not found: {gen_tasks} — build first")

    # ---- benchmark params ----------------------------------------------------
    bench = cfg["benchmark"]
    transports = [t.strip().lower() for t in bench["transports"].split(",")]
    client_counts = [int(n.strip()) for n in bench["client_counts"].split(",")]
    total_tasks = int(bench.get("total_tasks", 100))
    dim_lower = int(bench.get("dim_lower", 5))
    dim_upper = int(bench.get("dim_upper", 20))

    for t in transports:
        if t not in ("areg", "grpc", "areg_many"):
            raise SystemExit(f"Unknown transport: {t!r}")

    # ---- header --------------------------------------------------------------
    print("=" * 60)
    print("  Docker Benchmark Orchestrator")
    print("=" * 60)
    print(f"  Repo:         {repo_dir}")
    print(f"  Results:      {results_dir}")
    print(f"  Transports:   {transports}")
    print(f"  Client cnts:  {client_counts}")
    print(f"  Total tasks:  {total_tasks}")
    print(f"  Dims:         [{dim_lower}, {dim_upper}]")
    print()

    if dry_run:
        print("DRY RUN — no commands will be executed.\n")
        for t in transports:
            for n in client_counts:
                print(f"  [{t.upper():>4}] clients={n:>4}  →  "
                      f"{results_dir}/{t}_out_{n}.csv")
        return

    # ---- generate ONE shared task file on the host ---------------------------
    shared_task = tasks_dir / "shared.txt"
    print(f"Generating {total_tasks} tasks → {shared_task}")
    subprocess.run(
        [str(gen_tasks), str(total_tasks), str(dim_lower),
         str(dim_upper), str(shared_task)],
        cwd=str(repo_dir), check=True,
    )
    print()

    # ---- ensure image is built -----------------------------------------------
    if not no_build:
        print("Building Docker image …")
        subprocess.run(
            ["docker", "compose", "build"],
            cwd=str(repo_dir), check=True,
        )
        print()

    # ---- outer loops ---------------------------------------------------------
    total = len(transports) * len(client_counts)
    idx = 0

    for transport in transports:
        port = "50000" if transport == "grpc" else "8182"

        for n in client_counts:
            idx += 1
            out_csv = f"{transport}_out_{n}.csv"
            banner = f"[{idx}/{total}] {transport.upper()}  clients={n}"
            print(f"{'─' * 60}")
            print(f"  {banner}")
            print(f"{'─' * 60}")

            # Write .env file — docker compose reads it automatically.
            # This is more reliable than passing env vars via subprocess.
            env_file = repo_dir / ".env"
            env_file.write_text(
                f"TRANSPORT={transport}\n"
                f"TASK_FILE=/app/tasks/shared.txt\n"
                f"CLIENTS={n}\n"
                f"PORT={port}\n"
                f"TASKS={total_tasks}\n"
                f"DIM_LOWER={dim_lower}\n"
                f"DIM_UPPER={dim_upper}\n"
            )

            print(f"  docker compose up  "
                  f"TRANSPORT={transport}  CLIENTS={n}  PORT={port}")

            t0 = time.monotonic()
            result = subprocess.run(
                ["docker", "compose", "up",
                 "--abort-on-container-exit",
                 "--force-recreate",
                 "--remove-orphans"],
                cwd=str(repo_dir),
                # Don't capture — let the user see server/client logs
                stdout=None, stderr=None,
            )
            elapsed = time.monotonic() - t0

            if result.returncode != 0:
                print(f"  WARNING: docker compose exited with "
                      f"code {result.returncode}")
            else:
                print(f"  Completed in {elapsed:.1f}s")

            # ---- collect results ----------------------------------------------
            host_csv = repo_dir / "results" / out_csv
            dest_csv = results_dir / out_csv

            if host_csv.exists():
                if dest_csv.exists():
                    dest_csv.unlink()
                shutil.copy2(host_csv, dest_csv)
                size = dest_csv.stat().st_size
                print(f"  Results: {dest_csv}  ({size:,} bytes)")
                host_csv.unlink()
            else:
                print(f"  WARNING: output {host_csv} not found — "
                      f"check container logs above")

            # Brief pause between runs so ports fully release
            time.sleep(1)

    # ---- cleanup -------------------------------------------------------------
    if shared_task.exists():
        shared_task.unlink()
    # Remove leftover docker networks/containers
    subprocess.run(
        ["docker", "compose", "down", "--remove-orphans"],
        cwd=str(repo_dir), capture_output=True,
    )

    print(f"\n{'=' * 60}")
    print(f"  Done. Results in: {results_dir}")
    print(f"{'=' * 60}")


# ── entry point ──────────────────────────────────────────────────────────────

def main() -> None:
    ap = argparse.ArgumentParser(
        description="Docker-based AREG vs gRPC benchmark orchestrator")
    ap.add_argument("--config", "-c", default="benchmark.ini",
                    help="Path to INI config (default: benchmark.ini)")
    ap.add_argument("--dry-run", action="store_true",
                    help="Print the plan without executing")
    ap.add_argument("--no-build", action="store_true",
                    help="Skip 'docker compose build' (use existing image)")
    args = ap.parse_args()

    config_path = Path(args.config).expanduser().resolve()
    if not config_path.exists():
        raise SystemExit(f"Config not found: {config_path}")

    run_benchmark(config_path, dry_run=args.dry_run,
                  no_build=args.no_build)


if __name__ == "__main__":
    main()
