"""Benchmark runner for AREG vs gRPC matrix-multiplication.

Delegates server/client lifecycle to the repo's shell scripts
(``start-server.sh`` / ``start-clients.sh``).  
Usage::

    python3 run_benchmark.py --config my.ini    # custom config
    python3 run_benchmark.py --dry-run          # print the plan only
"""

from __future__ import annotations

import argparse
import configparser
import os
import re
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path


# ── helpers ──────────────────────────────────────────────────────────────────

def _resolve(path_str: str, base: Path, config_dir: Path) -> Path:
    """Expand ``~`` and resolve relative to *base*, falling back to *config_dir*."""
    s = os.path.expanduser(path_str)
    p = Path(s)
    if p.is_absolute():
        return p
    return (base / p).resolve() if base.is_dir() else (config_dir / p).resolve()


def _get_local_ip() -> str:
    """Best-effort primary LAN IP."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s.connect(("8.8.8.8", 80))
            return s.getsockname()[0]
        except OSError:
            return "127.0.0.1"


def _get_public_ip() -> str:
    """Public IP via ``curl -s ip.me``.  Falls back to the LAN IP on failure."""
    try:
        r = subprocess.run(["curl", "-s", "--max-time", "5", "ip.me"],
                           capture_output=True, text=True, timeout=6)
        if r.returncode == 0 and r.stdout.strip():
            return r.stdout.strip()
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass
    return _get_local_ip()


def _ssh_base(host: str, user: str, port: int) -> list[str]:
    """Prefix for an ``ssh`` invocation (no command)."""
    cmd = ["ssh", "-p", str(port)]
    if user:
        cmd.append(f"{user}@{host}")
    else:
        cmd.append(host)
    cmd.extend(["-o", "StrictHostKeyChecking=accept-new",
                "-o", "BatchMode=yes",
                "-o", "ConnectTimeout=10"])
    return cmd


def _ssh_bg(host: str, user: str, port: int, remote_cmd: str) -> subprocess.Popen:
    """Run *remote_cmd* via SSH in the background (detached remote side)."""
    wrapper = f"nohup bash -c '{remote_cmd}' </dev/null >/dev/null 2>&1 &"
    ssh = _ssh_base(host, user, port)
    ssh.append(wrapper)
    return subprocess.Popen(ssh, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)


def _wait_for_port(host: str, port: int, timeout: float = 60,
                   proc: subprocess.Popen | None = None) -> bool:
    """Poll until *host:port* accepts connections, or *proc* dies.

    Returns ``True`` once a TCP connection succeeds, ``False`` if the
    deadline expires or the server process exits before the port opens.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc is not None and proc.poll() is not None:
            return False  # server died before port opened
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return True
        except (ConnectionRefusedError, OSError):
            time.sleep(0.5)
    return False


# ── main ─────────────────────────────────────────────────────────────────────

def run_benchmark(config_path: Path, dry_run: bool = False) -> None:
    cfg = configparser.ConfigParser()
    cfg.read(config_path)
    config_dir = config_path.parent.resolve()

    # ---- paths ---------------------------------------------------------------
    repo_dir = _resolve(cfg["paths"]["repo_dir"], Path.cwd(), config_dir)
    results_dir = _resolve(cfg["paths"].get("results_dir", "results"),
                           repo_dir, config_dir)
    results_dir.mkdir(parents=True, exist_ok=True)

    start_server_sh = repo_dir / "start-server.sh"
    start_clients_sh = repo_dir / "start-clients.sh"
    gen_tasks = repo_dir / "build" / "common" / "gen-tasks"
    if not start_server_sh.exists():
        raise SystemExit(f"Missing: {start_server_sh}")
    if not start_clients_sh.exists():
        raise SystemExit(f"Missing: {start_clients_sh}")
    if not gen_tasks.exists():
        raise SystemExit(f"Missing: {gen_tasks} — build first")

    # ---- remote --------------------------------------------------------------
    remote = cfg["remote"]
    remote_host = remote.get("host", "").strip()
    remote_user = remote.get("user", "").strip()
    remote_port = int(remote.get("port", 22))
    use_remote = bool(remote_host)

    if use_remote:
        remote_repo = remote.get("repo_dir", str(repo_dir))
        if not remote_repo.startswith("/") and not remote_repo.startswith("~"):
            remote_repo = str(repo_dir)  # "." → local repo path
        remote_repo = os.path.expanduser(remote_repo)
        remote_clients_sh = f"{remote_repo}/start-clients.sh"
    else:
        remote_repo = ""  # unused in local-only mode

    # ---- server --------------------------------------------------------------
    srv = cfg["server"]
    bind_address = srv.get("bind_address", "0.0.0.0")
    grpc_port = int(srv.get("grpc_port", 50000))
    areg_port = int(srv.get("areg_port", 8182))
    startup_wait = int(srv.get("startup_wait", 3))

    # ---- benchmark -----------------------------------------------------------
    bench = cfg["benchmark"]
    transports = [t.strip().lower() for t in bench["transports"].split(",")]
    client_counts = [int(n.strip()) for n in bench["client_counts"].split(",")]
    total_tasks = int(bench.get("total_tasks", 100))
    dim_lower = int(bench.get("dim_lower", 5))
    dim_upper = int(bench.get("dim_upper", 20))

    # ---- validate ------------------------------------------------------------
    for t in transports:
        if t not in ("areg", "grpc", "areg_many"):
            raise SystemExit(f"Unknown transport: {t!r}")

    local_ip = _get_public_ip() if use_remote else _get_local_ip()

    print("=" * 60)
    print("  AREG vs gRPC Benchmark Runner")
    print("=" * 60)
    print(f"  Repo:         {repo_dir}")
    print(f"  Results:      {results_dir}")
    print(f"  Transports:   {transports}")
    print(f"  Client cnts:  {client_counts}")
    print(f"  Total tasks:  {total_tasks}")
    print(f"  Dims:         [{dim_lower}, {dim_upper}]")
    print(f"  Local IP:     {local_ip}")
    print(f"  gRPC port:    {grpc_port}")
    print(f"  AREG port:    {areg_port}")
    if use_remote:
        print(f"  Remote:       {remote_user}@{remote_host}:{remote_port}")
        print(f"  Remote repo:  {remote_repo}")
    else:
        print(f"  Remote:       (none — local-only mode)")
    print()

    if dry_run:
        print("DRY RUN — no commands will be executed.\n")
        for t in transports:
            for n in client_counts:
                print(f"  [{t.upper():>4}] clients={n:>4}  →  "
                      f"{results_dir}/{t}_out_{n}.csv")
        return

    # ---- generate ONE shared task file ---------------------------------------
    task_file = repo_dir / "tasks_bench.txt"
    print(f"Generating {total_tasks} tasks → {task_file}")
    subprocess.run(
        [str(gen_tasks), str(total_tasks), str(dim_lower),
         str(dim_upper), str(task_file)],
        check=True,
    )
    print()

    # ---- outer loops ---------------------------------------------------------
    total = len(transports) * len(client_counts)
    idx = 0

    for transport in transports:
        for n in client_counts:
            idx += 1
            out_csv = f"{transport}_out_{n}.csv"
            banner = f"[{idx}/{total}] {transport.upper()}  clients={n}"
            print(f"{'─' * 60}")
            print(f"  {banner}")
            print(f"{'─' * 60}")

            # Address strings for server and client scripts
            if transport in ["areg", "areg_many"]:
                server_addr = f"{bind_address}:{areg_port}"
                # Clients must reach the router at the actual host IP
                client_addr = f"{local_ip}:{areg_port}"
            else:
                server_addr = f"{bind_address}:{grpc_port}"
                client_addr = f"{local_ip}:{grpc_port}"

            # ---- start server (background) ----------------------------------
            server_cmd = [
                str(start_server_sh),
                "-t", transport,
                "-f", str(task_file),
                "-a", server_addr,
                "-o", out_csv,
            ]
            print(f"  Server: {' '.join(server_cmd)}")
            server_proc = subprocess.Popen(
                server_cmd,
                cwd=str(repo_dir),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )

            # ---- wait for server to be ready --------------------------------
            server_host_part = server_addr.split(":")[0]
            server_port_part = int(server_addr.split(":")[1])
            # 0.0.0.0 can't be connected to directly — use loopback instead
            health_host = ("127.0.0.1" if server_host_part == "0.0.0.0"
                           else server_host_part)
            timeout = max(startup_wait, 120)  # floor of 30 s for large files
            print(f"  Waiting for server on {health_host}:{server_port_part} "
                  f"(timeout {timeout}s) …")
            if not _wait_for_port(health_host, server_port_part,
                                  timeout=timeout, proc=server_proc):
                out = server_proc.stdout.read() if server_proc.stdout else b""
                rc = server_proc.returncode
                raise RuntimeError(
                    f"Server failed to start on "
                    f"{health_host}:{server_port_part} "
                    f"(exit code: {rc}):\n"
                    f"{out.decode(errors='replace')}"
                )

            # ---- start clients (background) ---------------------------------
            client_proc = None
            try:
                if use_remote:
                    # SSH → remote → start-clients.sh (detached on remote)
                    remote_cmd = (
                        f"cd {remote_repo} && "
                        f"./start-clients.sh -t {transport} "
                        f"-n {n} -a {client_addr}"
                    )
                    print(f"  Clients: ssh→{remote_host} {n}×{transport} → {client_addr}")
                    client_proc = _ssh_bg(remote_host, remote_user,
                                          remote_port, remote_cmd)
                else:
                    client_cmd = [
                        str(start_clients_sh),
                        "-t", transport,
                        "-n", str(n),
                        "-a", client_addr,
                    ]
                    print(f"  Clients: {' '.join(client_cmd)}")
                    client_proc = subprocess.Popen(
                        client_cmd,
                        cwd=str(repo_dir),
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )

                # ---- wait for server to finish -----------------------------
                print(f"  Waiting for server (pid {server_proc.pid}) …")
                try:
                    server_proc.wait(timeout=3000)
                except subprocess.TimeoutExpired:
                    print("  TIMEOUT — killing server")
                    server_proc.kill()
                    server_proc.wait()

                print(f"  Server exited (code={server_proc.returncode})")

                # Drain any remaining stdout
                if server_proc.stdout:
                    for line in server_proc.stdout.read().decode(
                            errors="replace").splitlines():
                        print(f"    [srv] {line}")

            finally:
                # ---- cleanup clients ---------------------------------------
                if client_proc and client_proc.poll() is None:
                    print(f"  Stopping client script (pid {client_proc.pid}) …")
                    client_proc.terminate()
                    try:
                        client_proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        client_proc.kill()
                        client_proc.wait()

            # ---- collect results ----------------------------------------------
            results_dest = results_dir / out_csv
            local_out = repo_dir / out_csv
            if local_out.exists():
                if results_dest.exists():
                    results_dest.unlink()
                shutil.copy2(local_out, results_dest)
                size = results_dest.stat().st_size
                print(f"  Results: {results_dest}  ({size:,} bytes)")
                local_out.unlink()
            else:
                print(f"  WARNING: output {local_out} not found")

    # ---- cleanup shared task file --------------------------------------------
    if task_file.exists():
        task_file.unlink()

    print(f"\n{'=' * 60}")
    print(f"  Done. Results in: {results_dir}")
    print(f"{'=' * 60}")


# ── entry point ──────────────────────────────────────────────────────────────

def main() -> None:
    ap = argparse.ArgumentParser(
        description="AREG vs gRPC benchmark runner (delegates to shell scripts)")
    ap.add_argument("--config", "-c", default="benchmark.ini",
                    help="Path to INI config (default: benchmark.ini)")
    ap.add_argument("--dry-run", action="store_true",
                    help="Print the plan without executing")
    args = ap.parse_args()

    config_path = Path(args.config).expanduser().resolve()
    if not config_path.exists():
        raise SystemExit(f"Config not found: {config_path}")

    run_benchmark(config_path, dry_run=args.dry_run)


if __name__ == "__main__":
    main()
