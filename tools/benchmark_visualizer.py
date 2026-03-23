#!/usr/bin/env python3
"""
SleakCraft Benchmark Visualizer
Usage: python benchmark_visualizer.py <benchmark.csv> [benchmark2.csv ...]
       python benchmark_visualizer.py benchmarks/     (all CSVs in folder)

Generates frame time, histogram, and system load graphs.
When multiple files are provided, they are overlaid for comparison.
"""

import sys
import csv
import math
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import matplotlib.patches as mpatches
import numpy as np
from pathlib import Path


# ──────────────────────────────────────────────────────────────────────────────
# Parsing
# ──────────────────────────────────────────────────────────────────────────────

def parse_benchmark(filepath):
    """Parse a benchmark CSV. Returns (frames, summary, header).

    frames   – list of dicts keyed by column name
    summary  – dict of # key → value strings
    header   – list of column name strings
    """
    frames  = []
    summary = {}
    header  = []

    with open(filepath, "r", encoding="utf-8", errors="replace") as f:
        reader = csv.reader(f)
        header = next(reader)

        for row in reader:
            if not row:
                continue

            if row[0].startswith("#"):
                key = row[0].lstrip("# ").strip()
                val = row[1].strip() if len(row) > 1 else ""
                summary[key] = val
                continue

            try:
                frame = {}
                for i, col in enumerate(header):
                    if i >= len(row):
                        break
                    col = col.strip()
                    val = row[i].strip()
                    # Detect int vs float vs string
                    try:
                        frame[col] = int(val)
                    except ValueError:
                        try:
                            frame[col] = float(val)
                        except ValueError:
                            frame[col] = val
                frames.append(frame)
            except (ValueError, IndexError):
                continue

    return frames, summary, header


def get_col(frames, *candidates, default=0.0):
    """Return a list of values for the first matching column name."""
    for name in candidates:
        if frames and name in frames[0]:
            return [f.get(name, default) for f in frames]
    return [default] * len(frames)


# ──────────────────────────────────────────────────────────────────────────────
# Label helpers
# ──────────────────────────────────────────────────────────────────────────────

def get_label(filepath):
    name  = Path(filepath).stem.replace("benchmark_", "")
    parts = name.split("_")
    renderer = parts[0].upper() if parts else "?"
    if len(parts) >= 3:
        t = parts[2]
        if len(t) == 6:
            t = f"{t[0:2]}:{t[2:4]}:{t[4:6]}"
        return f"{renderer} ({t})"
    return renderer


def get_time_col(frames):
    """Return the time column (supports both old 'Time' and new 'Time_s')."""
    for name in ("Time_s", "Time"):
        if frames and name in frames[0]:
            return [f[name] for f in frames]
    return list(range(len(frames)))


def get_frametime_col(frames):
    return get_col(frames, "FrameTime_ms", "frametime")


def get_fps_col(frames):
    return get_col(frames, "FPS", "fps")


def get_cpu_col(frames):
    return get_col(frames, "CPU_%", "cpu")


def get_ram_col(frames):
    return get_col(frames, "RAM_MB", "ram")


# ──────────────────────────────────────────────────────────────────────────────
# Statistics helpers
# ──────────────────────────────────────────────────────────────────────────────

def percentile(data, p):
    if not data:
        return 0.0
    s = sorted(data)
    idx = int(p / 100.0 * (len(s) - 1))
    return s[min(idx, len(s) - 1)]


def spike_count(data, threshold):
    return sum(1 for v in data if v > threshold)


def compute_stats(frames):
    """Compute full stats dict from frame data (skip frame 0)."""
    data = frames[1:] if len(frames) > 1 else frames
    ft   = get_frametime_col(data)
    fps  = get_fps_col(data)

    if not ft:
        return {}

    avg_ft = np.mean(ft)
    return {
        "count"    : len(frames),
        "duration" : get_time_col(frames)[-1] if frames else 0,
        "fps_min"  : min(fps),
        "fps_max"  : max(fps),
        "fps_avg"  : np.mean(fps),
        "ft_min"   : min(ft),
        "ft_max"   : max(ft),
        "ft_avg"   : avg_ft,
        "ft_p50"   : percentile(ft, 50),
        "ft_p95"   : percentile(ft, 95),
        "ft_p99"   : percentile(ft, 99),
        "ft_stdev" : np.std(ft),
        "sp16"     : spike_count(ft, 16.67),
        "sp33"     : spike_count(ft, 33.33),
        "sp50"     : spike_count(ft, 50.0),
        "cpu_avg"  : np.mean(get_cpu_col(data)),
        "ram_avg"  : np.mean(get_ram_col(data)),
    }


# ──────────────────────────────────────────────────────────────────────────────
# Console output
# ──────────────────────────────────────────────────────────────────────────────

SYSINFO_KEYS = ("CPU", "CPU_Cores", "Total_RAM_GB", "GPU", "GPU_VRAM_GB", "OS")

def print_summary(filepath, frames, summary):
    label = get_label(filepath)
    print(f"\n{'═' * 62}")
    print(f"  {label}")
    print(f"{'═' * 62}")

    # Prefer the pre-computed summary block from the CSV
    if summary:
        # Performance block
        perf_keys = (
            "Frames", "Duration_s", "Renderer", "VSync", "MSAA",
            "FPS_Min", "FPS_Max", "FPS_Avg",
            "FrameTime_Min_ms", "FrameTime_Max_ms", "FrameTime_Avg_ms",
            "FrameTime_P50_ms", "FrameTime_P95_ms", "FrameTime_P99_ms",
            "FrameTime_Stdev_ms",
            "Spikes_16ms", "Spikes_33ms", "Spikes_50ms",
            "Triangles_Avg", "CPU_Avg_%", "RAM_Avg_MB",
        )
        for k in perf_keys:
            if k in summary:
                print(f"  {k:30s} {summary[k]}")

        # Custom metrics
        for k, v in summary.items():
            if k not in perf_keys and k not in SYSINFO_KEYS and "---" not in k:
                print(f"  {k:30s} {v}")

        # System info block
        if any(k in summary for k in SYSINFO_KEYS):
            print(f"  {'─' * 50}")
            for k in SYSINFO_KEYS:
                if k in summary:
                    print(f"  {k:30s} {summary[k]}")
    else:
        # Fall back to computing from raw frames
        s = compute_stats(frames)
        if s:
            n = s["count"]
            print(f"  {'Frames':30s} {n}")
            print(f"  {'Duration_s':30s} {s['duration']:.2f}")
            print(f"  {'FPS Min/Max/Avg':30s} {s['fps_min']} / {s['fps_max']} / {s['fps_avg']:.1f}")
            print(f"  {'FT Avg/P50/P95/P99':30s} {s['ft_avg']:.2f} / {s['ft_p50']:.2f} / {s['ft_p95']:.2f} / {s['ft_p99']:.2f} ms")
            print(f"  {'FT Stdev (jitter)':30s} {s['ft_stdev']:.3f} ms")
            print(f"  {'Spikes >16ms/>33ms/>50ms':30s} {s['sp16']} / {s['sp33']} / {s['sp50']}")
            print(f"  {'CPU Avg':30s} {s['cpu_avg']:.1f} %")
            print(f"  {'RAM Avg':30s} {s['ram_avg']:.1f} MB")


# ──────────────────────────────────────────────────────────────────────────────
# Plotting
# ──────────────────────────────────────────────────────────────────────────────

COLORS = [
    "#2196F3", "#F44336", "#4CAF50", "#FF9800",
    "#9C27B0", "#00BCD4", "#E91E63", "#8BC34A",
]

SPIKE_BUCKETS = [
    ("< 8 ms",   0,      8,      "#4CAF50"),
    ("8–16 ms",  8,      16.67,  "#8BC34A"),
    ("16–33 ms", 16.67,  33.33,  "#FF9800"),
    ("33–50 ms", 33.33,  50.0,   "#F44336"),
    ("> 50 ms",  50.0,   1e9,    "#9C27B0"),
]


def smooth(data, window):
    if window <= 1:
        return data
    kernel = np.ones(window) / window
    return np.convolve(data, kernel, mode="valid")


def plot_benchmarks(filepaths):
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

    fig, axes = plt.subplots(3, 1, figsize=(15, 13))
    fig.suptitle("SleakCraft Benchmark", fontsize=16, fontweight="bold", y=0.98)

    ax_ft   = axes[0]   # Frame time over time
    ax_hist = axes[1]   # Frame time distribution histogram
    ax_sys  = axes[2]   # CPU % + RAM usage

    # ── Plot 1: Frame Time over time ─────────────────────────────────────────
    for i, (fp, frames, summary, header) in enumerate(all_data):
        times = get_time_col(frames)
        ft    = get_frametime_col(frames)
        color = COLORS[i % len(COLORS)]
        label = get_label(fp)

        # Raw trace (faint)
        ax_ft.plot(times, ft, color=color, alpha=0.18, linewidth=0.5)

        # Smoothed line
        w = max(1, len(ft) // 150)
        if w > 1:
            ax_ft.plot(times[w - 1:], smooth(ft, w), color=color,
                       linewidth=1.8, label=label)
        else:
            ax_ft.plot(times, ft, color=color, linewidth=1.8, label=label)

        # Shade spike regions (>33ms) in a faint fill per-renderer
        ft_arr  = np.array(ft)
        t_arr   = np.array(times)
        spike_mask = ft_arr > 33.33
        ax_ft.fill_between(t_arr, 0, ft_arr, where=spike_mask,
                           color=color, alpha=0.12)

        # Avg + P99 annotation
        data_after0 = ft[1:] if len(ft) > 1 else ft
        avg_ft = np.mean(data_after0)
        p99_ft = percentile(data_after0, 99)
        ax_ft.axhline(avg_ft, color=color, linestyle="--", alpha=0.5, linewidth=0.9)
        ax_ft.text(times[-1], avg_ft, f"  avg {avg_ft:.1f}", color=color,
                   fontsize=8, va="center")
        ax_ft.axhline(p99_ft, color=color, linestyle=":", alpha=0.4, linewidth=0.8)
        ax_ft.text(times[-1], p99_ft, f"  P99 {p99_ft:.1f}", color=color,
                   fontsize=7, va="center", alpha=0.7)

    # Reference lines
    for threshold, fps_label, line_color in ((16.67, "60 FPS", "#4CAF50"),
                                              (33.33, "30 FPS", "#FF9800"),
                                              (50.0,  "20 FPS", "#F44336")):
        ax_ft.axhline(threshold, color=line_color, linestyle=":", alpha=0.55, linewidth=1)
        ax_ft.text(0, threshold + 0.5, f" {fps_label}", color=line_color,
                   fontsize=8, va="bottom")

    ax_ft.set_ylabel("Frame Time (ms)", fontsize=11)
    ax_ft.set_xlabel("Time (s)", fontsize=10)
    ax_ft.legend(loc="upper right", fontsize=9)
    ax_ft.grid(True, alpha=0.25)
    ax_ft.set_ylim(bottom=0)
    ax_ft.set_title("Frame Time Over Time", fontsize=11, loc="left", pad=4)

    # ── Plot 2: Frame Time Distribution (stacked bar) ────────────────────────
    n_renderers  = len(all_data)
    bar_width    = 0.7 / max(n_renderers, 1)
    x_positions  = np.arange(len(SPIKE_BUCKETS))
    legend_patches = []

    for i, (fp, frames, summary, header) in enumerate(all_data):
        ft_data = get_frametime_col(frames[1:]) if len(frames) > 1 else get_frametime_col(frames)
        total   = max(len(ft_data), 1)
        label   = get_label(fp)
        color   = COLORS[i % len(COLORS)]

        bucket_pcts = []
        for (_, lo, hi, _) in SPIKE_BUCKETS:
            count = sum(1 for v in ft_data if lo <= v < hi)
            bucket_pcts.append(count / total * 100.0)

        offset = (i - n_renderers / 2.0 + 0.5) * bar_width
        ax_hist.bar(x_positions + offset, bucket_pcts, width=bar_width,
                    label=label, color=color, alpha=0.82, edgecolor="white",
                    linewidth=0.5)

        # Value labels on bars
        for xi, pct in zip(x_positions + offset, bucket_pcts):
            if pct >= 1.0:
                ax_hist.text(xi, pct + 0.3, f"{pct:.1f}%", ha="center",
                             va="bottom", fontsize=7, color=color)

    ax_hist.set_xticks(x_positions)
    ax_hist.set_xticklabels([b[0] for b in SPIKE_BUCKETS], fontsize=10)
    ax_hist.set_ylabel("% of Frames", fontsize=11)
    ax_hist.set_xlabel("Frame Time Bucket", fontsize=10)
    ax_hist.legend(loc="upper right", fontsize=9)
    ax_hist.grid(True, axis="y", alpha=0.25)
    ax_hist.set_ylim(bottom=0)
    ax_hist.set_title("Frame Time Distribution", fontsize=11, loc="left", pad=4)

    # Colour the bucket labels to match the danger colours
    for tick, (_, _, _, bcolor) in zip(ax_hist.get_xticklabels(), SPIKE_BUCKETS):
        tick.set_color(bcolor)

    # ── Plot 3: CPU % + RAM usage ─────────────────────────────────────────────
    ax_ram = ax_sys.twinx()

    for i, (fp, frames, summary, header) in enumerate(all_data):
        times = get_time_col(frames)
        cpu   = get_cpu_col(frames)
        ram   = get_ram_col(frames)
        color = COLORS[i % len(COLORS)]
        label = get_label(fp)

        w = max(1, len(cpu) // 150)
        cpu_s = smooth(cpu, w) if w > 1 else cpu
        ram_s = smooth(ram, w) if w > 1 else ram
        t_s   = times[w - 1:] if w > 1 else times

        ax_sys.plot(t_s, cpu_s, color=color, linewidth=1.6,
                    label=f"{label} CPU")
        ax_ram.plot(t_s, ram_s, color=color, linewidth=1.2, linestyle="--",
                    alpha=0.65, label=f"{label} RAM")

    ax_sys.set_ylabel("CPU Usage (%)", fontsize=11)
    ax_ram.set_ylabel("RAM Usage (MB)", fontsize=11)
    ax_sys.set_xlabel("Time (s)", fontsize=10)
    ax_sys.set_ylim(0, 105)
    ax_sys.grid(True, alpha=0.25)
    ax_sys.set_title("System Load", fontsize=11, loc="left", pad=4)

    # Combined legend
    lines1, labels1 = ax_sys.get_legend_handles_labels()
    lines2, labels2 = ax_ram.get_legend_handles_labels()
    ax_sys.legend(lines1 + lines2, labels1 + labels2, loc="upper right", fontsize=8)

    # ── System info text box (first file's hardware info) ────────────────────
    if all_data:
        _, _, summary, _ = all_data[0]
        info_lines = []
        for k in SYSINFO_KEYS:
            if k in summary:
                info_lines.append(f"{k}: {summary[k]}")
        if info_lines:
            info_text = "\n".join(info_lines)
            fig.text(0.01, 0.01, info_text,
                     fontsize=7, color="#666666",
                     verticalalignment="bottom",
                     bbox=dict(boxstyle="round,pad=0.3", facecolor="white",
                               edgecolor="#cccccc", alpha=0.8))

    plt.tight_layout(rect=[0, 0.04, 1, 0.97])

    out_dir  = Path(filepaths[0]).parent
    out_path = out_dir / "benchmark_graph.png"
    plt.savefig(out_path, dpi=150, bbox_inches="tight")
    print(f"\nGraph saved to: {out_path}")
    plt.show()


# ──────────────────────────────────────────────────────────────────────────────
# Entry point
# ──────────────────────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 2:
        print("Usage: python benchmark_visualizer.py <benchmark.csv> [...]")
        print("       python benchmark_visualizer.py benchmarks/")
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
