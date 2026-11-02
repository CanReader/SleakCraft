#!/usr/bin/env python3
"""Compares benchmark CSVs written by scripts/bench_run.sh.

Usage: scripts/bench_compare.py <out_dir> [--from N] [--base LABEL]
Frame time comes from Time_s deltas, since capture mode fixes FrameTime_ms.
"""
import argparse
import collections
import csv
import hashlib
import pathlib
import re
import statistics

TAG = re.compile(r"(?P<label>.+)_(?P<renderer>[a-z0-9]+)_(?P<res>\d+x\d+)_(?P<run>\d+)$")
GPU_COLS = ["GPU_shadow_ms", "GPU_gbuffer_ms", "GPU_ssao_ms", "GPU_lighting_ms",
            "GPU_forward_ms", "GPU_taa_ms", "GPU_ssr_ms", "GPU_total_ms"]


def percentile(values, p):
    s = sorted(values)
    k = (len(s) - 1) * p
    lo, hi = int(k), min(int(k) + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def load_run(path, first_frame):
    with open(path, newline="") as f:
        rows = [r for r in csv.DictReader(f)]
    rows = [r for r in rows if r["Frame"].isdigit() and int(r["Frame"]) >= first_frame][:-5]
    times = [float(r["Time_s"]) for r in rows]
    frame_ms = [(b - a) * 1000.0 for a, b in zip(times, times[1:])]
    out = {
        "frame_mean": statistics.fmean(frame_ms),
        "frame_p50": percentile(frame_ms, 0.50),
        "frame_p99": percentile(frame_ms, 0.99),
        "tris": statistics.fmean(float(r["Triangles"]) for r in rows),
        "cull_ms": statistics.fmean(float(r.get("Cull_RasterMs", 0) or 0) for r in rows),
        "fps": 1000.0 / statistics.fmean(frame_ms),
    }
    if "CPU_frame_ms" in rows[0]:
        out["cpu_frame_ms"] = statistics.fmean(float(r["CPU_frame_ms"]) for r in rows)
    for c in GPU_COLS:
        if c in rows[0]:
            out[c] = statistics.fmean(float(r[c]) for r in rows)
    return out


def md5(path):
    return hashlib.md5(path.read_bytes()).hexdigest()[:10] if path.exists() else "-"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir")
    ap.add_argument("--from", dest="first", type=int, default=800)
    ap.add_argument("--base", default="base")
    args = ap.parse_args()

    groups = collections.defaultdict(list)
    images = collections.defaultdict(set)
    for p in sorted(pathlib.Path(args.out_dir).glob("*.csv")):
        m = TAG.match(p.stem)
        if not m:
            continue
        key = (m["renderer"], m["res"], m["label"])
        groups[key].append(load_run(p, args.first))
        images[key].add(md5(p.with_suffix(".ppm")))

    metrics = ["fps", "frame_mean", "frame_p50", "frame_p99", "cpu_frame_ms", *GPU_COLS, "cull_ms", "tris"]
    scenarios = sorted({(r, res) for r, res, _ in groups})
    for renderer, res in scenarios:
        labels = sorted({l for r, s, l in groups if (r, s) == (renderer, res)},
                        key=lambda l: (l != args.base, l))
        print(f"\n== {renderer} {res} (frames >= {args.first}) ==")
        print(f"{'metric':<16}" + "".join(f"{l:>22}" for l in labels) + "   delta")
        for metric in metrics:
            cells, means = [], []
            for l in labels:
                vals = [run[metric] for run in groups[(renderer, res, l)] if metric in run]
                if not vals:
                    cells.append(f"{'-':>22}")
                    means.append(None)
                    continue
                mu = statistics.fmean(vals)
                sd = statistics.stdev(vals) if len(vals) > 1 else 0.0
                means.append(mu)
                cells.append(f"{mu:>14.3f} ±{sd:>6.3f}")
            delta = ""
            if len(means) >= 2 and means[0] and means[-1] is not None:
                delta = f"{(means[-1] - means[0]) / means[0] * 100:+.1f}%"
            print(f"{metric:<16}" + "".join(cells) + f"   {delta}")
        for l in labels:
            runs = len(groups[(renderer, res, l)])
            print(f"  {l}: {runs} runs, capture md5 {sorted(images[(renderer, res, l)])}")


if __name__ == "__main__":
    main()
