#!/usr/bin/env bash
# Stage 2 motivation probe: sweeps burst_len x window for a trace made of
# short sequential runs separated by random jumps, on top of the exact same
# Stage 1 fixed-window buffer_pool/readahead used by stage1_bench.sh.
#
# stage1_bench.sh only tests "sequential for the entire run" and "random for
# the entire run" -- both are cases where a single fixed window is either
# obviously right or obviously irrelevant. This script targets the gap
# between them: bursts short enough that a fixed window can overshoot past
# the end of a burst (wasted prefetch, tight capacity -> eviction pressure)
# or undershoot a long burst (missed hits).
#
# Expects build/interrupted_bench to accept:
#   interrupted_bench <file_path> <file_size_bytes> <block_size> <capacity_blocks> <window> <burst_len> <num_accesses> <repeats>
# and print one CSV line to stdout:
#   burst_len,window,capacity_blocks,file_size,block_size,num_accesses,repeats,mean_elapsed_sec,stddev_elapsed_sec,mean_throughput_mb_s,mean_hit_rate
set -euo pipefail

cd "$(dirname "$0")/.."

make

BIN=build/interrupted_bench
TEST_FILE="$(mktemp -t prefetch_sim_interrupted.XXXXXX)"
FILE_SIZE=$((512 * 1024 * 1024)) # 512MB
BLOCK_SIZE=4096
CAPACITY_BLOCKS=64 # kept tight (== largest window swept) so overshoot pressure shows up
NUM_ACCESSES=8000
REPEATS=5
OUT_CSV=results/interrupted.csv

cleanup() {
    rm -f "$TEST_FILE"
}
trap cleanup EXIT

echo "burst_len,window,capacity_blocks,file_size,block_size,num_accesses,repeats,mean_elapsed_sec,stddev_elapsed_sec,mean_throughput_mb_s,mean_hit_rate" > "$OUT_CSV"

for burst_len in 4 16 64 256; do
    for window in 0 4 16 64; do
        "$BIN" "$TEST_FILE" "$FILE_SIZE" "$BLOCK_SIZE" "$CAPACITY_BLOCKS" "$window" "$burst_len" "$NUM_ACCESSES" "$REPEATS" >> "$OUT_CSV"
    done
done

echo "results written to $OUT_CSV"
