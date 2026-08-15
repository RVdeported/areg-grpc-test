#!/usr/bin/env python3
"""Benchmark analysis tool for AREG vs gRPC matrix-multiplication benchmarks.

Scans a folder for ``{transport}_out_{N}.csv`` files, computes derived
timing metrics, and produces plots comparing the two transports.

Usage::

    python3 analyze.py                  # uses ./samples by default
    python3 analyze.py --folder results
    python3 analyze.py --folder results --output report.pdf
    python3 analyze.py --no-show        # save only, don't display
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
import numpy as np
import pandas as pd
import seaborn as sns
from matplotlib.figure import Figure

# ── constants ────────────────────────────────────────────────────────────────
_METRICS: dict[str, str] = {
    "ttl":        "ts_srv_rec - ts_srv_snd",
    # "cli_total":  "ts_cli_snd - ts_cli_rec",
    "cli_ser":  "ts_cli_tsk - ts_cli_rec",
    "cli_compute": "ts_cli_snd - ts_cli_tsk",
    "net_srv2cli": "ts_cli_rec - ts_srv_snd",
    "net_cli2srv": "ts_srv_rec - ts_cli_snd",
    "ram":        "ram_used_kb",
    # "cpu":        "cpu_usage_percent",
}

_LABELS: dict[str, str] = {
    "ttl":          "Total round-trip (µs)",
    # "cli_total":    "Client wall-clock (µs)",
    "cli_ser":      "Client serialization delay (µs)",
    "cli_compute":  "Client compute time (µs)",
    "net_srv2cli":  "Network srv→cli (µs)",
    "net_cli2srv":  "Network cli→srv (µs)",
    "ram":          "System RAM used (kB)",
    # "cpu":          "System CPU usage (%)",
}

COLORS: dict[str, str] = {"areg": "#2ca02c", "grpc": "#1f77b4"}
MARKERS: dict[str, str] = {"areg": "o", "grpc": "s"}


# ── helpers ──────────────────────────────────────────────────────────────────
def _parse_filename(name: str) -> tuple[str, int] | None:
    """Return ``(transport, n_workers)`` or *None* if no match."""
    m = re.match(r"^(.+)_out_(\d+)\.csv$", name)
    if m is None:
        return None
    return m.group(1).lower(), int(m.group(2))


def load_folder(folder: Path) -> pd.DataFrame:
    """Read all ``*_out_*.csv`` files in *folder* into a single DataFrame.

    Columns added: ``transport``, ``n_workers``.
    Sentinel rows (n=m=k=0) are filtered out.
    """
    frames: list[pd.DataFrame] = []
    for p in sorted(folder.glob("*_out_*.csv")):
        info = _parse_filename(p.name)
        if info is None:
            print(f"  [skip] {p.name!r} — does not match pattern", file=sys.stderr)
            continue
        transport, nw = info
        df = pd.read_csv(p)
        df["transport"] = transport
        df["n_workers"] = nw
        # Filter sentinel rows used by AREG as keep-alive pings
        df = df[~((df["n"] == 0) & (df["m"] == 0) & (df["k"] == 0))]
        # df["cpu_usage_percent"] *= 100
        frames.append(pd.DataFrame(df))
        print(f"  [load] {p.name:<24s}  {len(df):>5d} rows  transport={transport}, N={nw}",
              file=sys.stderr)

    if not frames:
        raise SystemExit(f"No matching CSV files found in {folder}")

    return pd.concat(frames, ignore_index=True)


def compute_metrics(df: pd.DataFrame) -> pd.DataFrame:
    """Add derived microsecond-duration columns."""
    for name, expr in _METRICS.items():
        df[name] = df.eval(expr)
    return df


def _make_figure() -> "tuple[Figure, np.ndarray]":
    """Create a 4×2 grid of Axes + a single shared legend."""
    fig, axes = plt.subplots(3, 3, figsize=(14, 20), constrained_layout=True)
    fig.suptitle("AREG vs gRPC — Matrix Multiply Benchmark",
                 fontsize=14, fontweight="bold", y=1.01)
    return fig, axes


def _bar_plot(ax, agg: pd.DataFrame, metric: str):
    """Draw a grouped bar chart with error bars on a single Axes."""
    transports = sorted(agg["transport"].unique())
    workers = sorted(agg["n_workers"].unique())
    n_trans = len(transports)
    width = 0.35
    x = np.arange(len(workers))
    offset = np.linspace(-width / 2, width / 2, n_trans) if n_trans > 1 else [0]

    for i, t in enumerate(transports):
        sub = agg[agg["transport"] == t].sort_values("n_workers")
        means = sub["mean"].values
        stds = sub["std"].values
        ax.bar(
            x + offset[i], means, width,
            yerr=stds, capsize=4,
            color=COLORS.get(t, f"C{i}"),
            edgecolor="black", linewidth=0.6,
            label=t.upper(),
        )

    ax.set_xlabel("Number of workers")
    ax.set_ylabel(_LABELS[metric])
    ax.set_title(_LABELS[metric])
    ax.set_xticks(x)
    ax.set_xticklabels(workers)
    ax.legend(loc="upper left", fontsize=8)
    ax.yaxis.set_major_formatter(mticker.FuncFormatter(lambda v, _: f"{v:,.2f}"))
    ax.grid(axis="y", alpha=0.3)


def _line_plot(ax, df: pd.DataFrame, metric: str):
    """Line plot with shaded std band — better for many N values."""
    for t in sorted(df["transport"].unique()):
        sub = df[df["transport"] == t].sort_values("n_workers")
        ax.plot(sub["n_workers"], sub["mean"],
                color=COLORS.get(t, None), marker=MARKERS.get(t, "o"),
                markersize=5, linewidth=1.5, label=t.upper())
        ax.fill_between(sub["n_workers"],
                        sub["mean"] - sub["std"],
                        sub["mean"] + sub["std"],
                        color=COLORS.get(t, None), alpha=0.15)

    ax.set_xlabel("Number of workers")
    ax.set_ylabel(_LABELS[metric])
    ax.set_title(_LABELS[metric])
    ax.legend(fontsize=8)
    ax.yaxis.set_major_formatter(mticker.FuncFormatter(lambda v, _: f"{v:,.2f}"))
    ax.grid(True, alpha=0.3)


# ── main ─────────────────────────────────────────────────────────────────────
def main() -> None:
    ap = argparse.ArgumentParser(description="Analyze AREG/gRPC benchmark CSVs")
    ap.add_argument("--folder", default="samples",
                    help="Folder with *_out_*.csv files (default: samples)")
    ap.add_argument("--output", "-o", default=None,
                    help="Save figure to this path (PNG/PDF/SVG)")
    ap.add_argument("--no-show", action="store_true",
                    help="Don't display the figure interactively")
    ap.add_argument("--style", choices=["bar", "line"], default="line",
                    help="Plot style: bar (few N) or line+shade (many N)")
    ap.add_argument("--dpi", type=int, default=150,
                    help="Output DPI (default: 150)")
    args = ap.parse_args()

    # Use a clean Seaborn theme
    sns.set_theme(style="whitegrid", context="notebook", font_scale=1.05)

    folder = Path(args.folder).expanduser().resolve()
    print(f"Scanning folder: {folder}", file=sys.stderr)

    raw = load_folder(folder)
    if raw.empty:
        raise SystemExit("No data loaded — aborting.")

    df = compute_metrics(raw)

    # ── Print summary stats ──────────────────────────────────────────────────
    print(file=sys.stderr)
    for metric in _METRICS:
        tbl = (
            df.groupby(["transport", "n_workers"])[metric]
              .agg(mean="mean", std="std")
              .reset_index()
        )
        tbl["std"] = tbl["std"].fillna(0)
        print(f"  [{metric}]", file=sys.stderr)
        print(tbl.to_string(index=False), file=sys.stderr)
        print(file=sys.stderr)

    # ── Build per-metric aggregates ──────────────────────────────────────────
    metrics_agg: dict[str, pd.DataFrame] = {}
    for metric in _METRICS:
        agg = (
            df.groupby(["transport", "n_workers"])[metric]
              .agg(mean="mean", std="std")
              .reset_index()
        )
        # NaN std for single-row groups → 0
        agg["std"] = agg["std"].fillna(0)
        metrics_agg[metric] = agg

    # ── Plot ─────────────────────────────────────────────────────────────────
    fig, axes = _make_figure()
    plotter = _line_plot if args.style == "line" else _bar_plot

    log_metrics = {"cli_ser", "cli_compute", "cli_total", "net_srv2cli", "net_cli2srv"}
    for (metric, label), ax in zip(_LABELS.items(), axes.flat):
        plotter(ax, metrics_agg[metric], metric)
        if metric in log_metrics:
            ax.set_yscale("log")

    # Remove empty subplot if odd number of metrics
    if len(_LABELS) < axes.size:
        for ax in axes.flat[len(_LABELS):]:
            ax.set_visible(False)

    fig.suptitle("AREG vs gRPC — Matrix Multiply Benchmark", fontsize=14, fontweight="bold")

    if args.output:
        fig.savefig(args.output, dpi=args.dpi, bbox_inches="tight")
        print(f"\nSaved to: {args.output}", file=sys.stderr)

    if not args.no_show:
        plt.show()


if __name__ == "__main__":
    main()
