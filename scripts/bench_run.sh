#!/usr/bin/env bash
# Deterministic benchmark runs (fixed 1/60 step, synchronous chunk loading).
# Usage: scripts/bench_run.sh <bin_dir> <label> <out_dir> [runs] [width] [height]
# Env: BENCH_FRAMES (4000), BENCH_RD (12), BENCH_SEED (4242), BENCH_RENDERER (vulkan),
#      BENCH_ARGS (extra game flags, e.g. --bench-postfx), BENCH_RUN_START (1),
#      BENCH_HEADLESS=1 runs under Xvfb (present is a CPU copy there, so only the
#      CPU_frame_ms and GPU_* columns are meaningful)
set -euo pipefail

BIN=$(realpath "$1")
LABEL=$2
OUT=$(realpath -m "$3")
RUNS=${4:-3}
W=${5:-1600}
H=${6:-900}
FRAMES=${BENCH_FRAMES:-4000}
RD=${BENCH_RD:-12}
SEED=${BENCH_SEED:-4242}
RENDERER=${BENCH_RENDERER:-vulkan}
WORLD=benchrun

mkdir -p "$OUT" "$BIN/benchmarks"
START=${BENCH_RUN_START:-1}
for i in $(seq "$START" $((START + RUNS - 1))); do
    tag="${LABEL}_${RENDERER}_${W}x${H}_${i}"
    rm -rf "$BIN/saves/$WORLD"
    marker=$(mktemp)
    launcher=()
    if [[ -n "${BENCH_HEADLESS:-}" ]]; then
        launcher=(env -u WAYLAND_DISPLAY SDL_VIDEO_DRIVER=x11
                  xvfb-run -a -s "-screen 0 ${W}x${H}x24")
    fi
    (cd "$BIN" && export LD_LIBRARY_PATH="$BIN${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" && \
        "${launcher[@]}" timeout 600 ./SleakCraft -r "$RENDERER" -w "$W" -h "$H" --no-vsync \
        -rd "$RD" -world "$WORLD" -seed "$SEED" --bench --gpuprofile \
        -capture "$OUT/$tag.ppm" -capture-frame "$FRAMES" ${BENCH_ARGS:-} \
        > "$OUT/$tag.log" 2>&1) || echo "run $tag exited with $?"
    csv=$(find "$BIN/benchmarks" -name '*.csv' -newer "$marker" | sort | tail -1)
    rm -f "$marker"
    if [[ -n "$csv" ]]; then
        mv "$csv" "$OUT/$tag.csv"
        echo "$tag done"
    else
        echo "$tag produced no csv"
    fi
    rm -rf "$BIN/saves/$WORLD"
done
