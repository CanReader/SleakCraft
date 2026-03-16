#!/usr/bin/env python3
"""
SleakCraft Benchmark Visualizer
Usage: python benchmark_visualizer.py <benchmark.csv> [benchmark2.csv ...]

Generates FPS and Frame Time graphs from benchmark CSV files.
When multiple files are provided, they are overlaid for comparison.
"""

import sys
import os
import csv
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
from pathlib import Path


def parse_benchmark(filepath):
    """Parse a benchmark CSV file, returning frame data and summary."""
    frames = []
    summary = {}

    with open(filepath, "r") as f:
        reader = csv.reader(f)
        header = next(reader)

        for row in reader:
            if not row or row[0].startswith("#"):
                # Parse summary lines
                if row and row[0].startswith("#"):
                    key = row[0].lstrip("# ").strip()
                    val = row[1].strip() if len(row) > 1 else ""
                    summary[key] = val
                continue

            try:
                frame = {
                    "frame": int(row[0]),
                    "time": float(row[1]),
                    "fps": int(row[2]),
                    "frametime": float(row[3]),
                    "vertices": int(row[4]),
                    "ram": float(row[5]),
                }
                # Parse custom metrics
                for i in range(6, min(len(row), len(header))):
                    frame[header[i]] = float(row[i])
                frames.append(frame)
            except (ValueError, IndexError):
                continue

    return frames, summary, header


def get_label(filepath):
    """Extract a readable label from filename."""
    name = Path(filepath).stem
    name = name.replace("benchmark_", "")
    # e.g. "dx11_20260315_185056" -> "DX11 18:50:56"
    parts = name.split("_")
    renderer = parts[0].upper()
    if len(parts) >= 3:
        time_str = parts[2]
        if len(time_str) == 6:
            time_str = f"{time_str[0:2]}:{time_str[2:4]}:{time_str[4:6]}"
        return f"{renderer} ({time_str})"
    return renderer


def print_summary(filepath, frames, summary):
    """Print summary stats to console."""
    label = get_label(filepath)
    print(f"\n{'='*60}")
    print(f"  {label}")
    print(f"{'='*60}")

    if summary:
        for key, val in summary.items():
            print(f"  {key}: {val}")
    elif frames:
        fps_vals = [f["fps"] for f in frames[1:]]  # skip frame 0
        ft_vals = [f["frametime"] for f in frames[1:]]
        vert_vals = [f["vertices"] for f in frames[1:]]
        ram_vals = [f["ram"] for f in frames[1:]]

        if fps_vals:
            print(f"  Frames: {len(frames)}")
            print(f"  Duration: {frames[-1]['time']:.2f}s")
            print(f"  FPS  - Min: {min(fps_vals):>6}  Max: {max(fps_vals):>6}  Avg: {np.mean(fps_vals):>8.1f}")
            print(f"  FT   - Min: {min(ft_vals):>6.2f}  Max: {max(ft_vals):>6.2f}  Avg: {np.mean(ft_vals):>8.2f} ms")
            print(f"  Verts  Avg: {np.mean(vert_vals):>14,.0f}")
            print(f"  RAM    Avg: {np.mean(ram_vals):>8.1f} MB")


def plot_benchmarks(filepaths):
    """Generate comparison graphs for one or more benchmark files."""
    all_data = []
    for fp in filepaths:
        frames, summary, header = parse_benchmark(fp)
        if not frames:
            print(f"Warning: No data in {fp}")
            continue
        all_data.append((fp, frames, summary, header))
        print_summary(fp, frames, summary)

    if not all_data:
        print("No valid benchmark data found.")
        return

    # Color palette
    colors = ["#2196F3", "#F44336", "#4CAF50", "#FF9800", "#9C27B0",
              "#00BCD4", "#E91E63", "#8BC34A", "#FF5722", "#673AB7"]

    fig, axes = plt.subplots(2, 1, figsize=(14, 9), sharex=False)
    fig.suptitle("SleakCraft Benchmark", fontsize=16, fontweight="bold")

    # --- FPS Graph ---
    ax_fps = axes[0]
    for i, (fp, frames, summary, header) in enumerate(all_data):
        times = [f["time"] for f in frames]
        fps = [f["fps"] for f in frames]
        color = colors[i % len(colors)]
        label = get_label(fp)

        ax_fps.plot(times, fps, color=color, alpha=0.3, linewidth=0.5)
        # Smoothed line (rolling average)
        window = max(1, len(fps) // 100)
        if window > 1:
            smoothed = np.convolve(fps, np.ones(window) / window, mode="valid")
            smoothed_t = times[window - 1:]
            ax_fps.plot(smoothed_t, smoothed, color=color, linewidth=2, label=label)
        else:
            ax_fps.plot(times, fps, color=color, linewidth=2, label=label)

        # Draw avg line
        avg_fps = np.mean([f["fps"] for f in frames[1:]])
        ax_fps.axhline(y=avg_fps, color=color, linestyle="--", alpha=0.5, linewidth=1)
        ax_fps.text(times[-1], avg_fps, f" {avg_fps:.0f}", color=color,
                    fontsize=9, va="center")

    ax_fps.set_ylabel("FPS", fontsize=12)
    ax_fps.set_xlabel("Time (s)", fontsize=10)
    ax_fps.legend(loc="upper right", fontsize=9)
    ax_fps.grid(True, alpha=0.3)
    ax_fps.set_ylim(bottom=0)
    ax_fps.yaxis.set_major_locator(ticker.MaxNLocator(integer=True))

    # --- Frame Time Graph ---
    ax_ft = axes[1]
    for i, (fp, frames, summary, header) in enumerate(all_data):
        times = [f["time"] for f in frames]
        ft = [f["frametime"] for f in frames]
        color = colors[i % len(colors)]
        label = get_label(fp)

        ax_ft.plot(times, ft, color=color, alpha=0.3, linewidth=0.5)
        window = max(1, len(ft) // 100)
        if window > 1:
            smoothed = np.convolve(ft, np.ones(window) / window, mode="valid")
            smoothed_t = times[window - 1:]
            ax_ft.plot(smoothed_t, smoothed, color=color, linewidth=2, label=label)
        else:
            ax_ft.plot(times, ft, color=color, linewidth=2, label=label)

        avg_ft = np.mean([f["frametime"] for f in frames[1:]])
        ax_ft.axhline(y=avg_ft, color=color, linestyle="--", alpha=0.5, linewidth=1)
        ax_ft.text(times[-1], avg_ft, f" {avg_ft:.1f}ms", color=color,
                    fontsize=9, va="center")

    # Target frame time lines
    ax_ft.axhline(y=16.67, color="green", linestyle=":", alpha=0.4, linewidth=1)
    ax_ft.text(0, 16.67, " 60 FPS", color="green", fontsize=8, va="bottom")
    ax_ft.axhline(y=33.33, color="orange", linestyle=":", alpha=0.4, linewidth=1)
    ax_ft.text(0, 33.33, " 30 FPS", color="orange", fontsize=8, va="bottom")

    ax_ft.set_ylabel("Frame Time (ms)", fontsize=12)
    ax_ft.set_xlabel("Time (s)", fontsize=10)
    ax_ft.legend(loc="upper right", fontsize=9)
    ax_ft.grid(True, alpha=0.3)
    ax_ft.set_ylim(bottom=0)

    plt.tight_layout()

    # Save next to the first benchmark file
    out_dir = Path(filepaths[0]).parent
    out_path = out_dir / "benchmark_graph.png"
    plt.savefig(out_path, dpi=150, bbox_inches="tight")
    print(f"\nGraph saved to: {out_path}")

    plt.show()


def main():
    if len(sys.argv) < 2:
        print("Usage: python benchmark_visualizer.py <benchmark.csv> [benchmark2.csv ...]")
        print("       python benchmark_visualizer.py benchmarks/  (all CSVs in folder)")
        sys.exit(1)

    paths = []
    for arg in sys.argv[1:]:
        p = Path(arg)
        if p.is_dir():
            paths.extend(sorted(p.glob("*.csv")))
        elif p.is_file():
            paths.append(p)
        else:
            print(f"Warning: {arg} not found")

    if not paths:
        print("No benchmark files found.")
        sys.exit(1)

    plot_benchmarks([str(p) for p in paths])


if __name__ == "__main__":
    main()
