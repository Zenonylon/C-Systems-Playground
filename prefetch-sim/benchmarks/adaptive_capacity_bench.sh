#!/usr/bin/env bash
# Stage 2 capacity probe (mirrors interrupted_capacity_bench.sh for Stage 1):
# tests whether AdaptiveReadahead's capacity-overrun cliff (docs/stage_progression.md,
# Stage 1->2 "이 근거의 한계" (b)) is something the algorithm itself avoids, or
# just something mixed_bench.sh's config happened to sidestep by keeping
# max_window <= capacity.
#
# max_window and burst_len are held fixed (max_window > the smallest
# capacity_blocks swept, so a fully-grown window can exceed capacity);
# capacity_blocks sweeps from well below max_window to well above it.
# adaptive_readahead.c has no capacity awareness at all (confirmed by
# reading the source -- AdaptiveReadahead doesn't even store a capacity
# field), so if a cliff appears here, it would confirm the algorithm has no
# self-protection and mixed_bench.sh's earlier result was a config artifact.
#
# Expects build/adaptive_bench to accept:
#   adaptive_bench <file_path> <file_size_bytes> <block_size> <capacity_blocks> <min_window> <max_window> <burst_len> <num_accesses> <repeats>
# and print one CSV line to stdout:
#   burst_len,min_window,max_window,capacity_blocks,file_size,block_size,num_accesses,repeats,mean_elapsed_sec,stddev_elapsed_sec,mean_throughput_mb_s,mean_hit_rate
set -euo pipefail

cd "$(dirname "$0")/.."

make

BIN=build/adaptive_bench
TEST_FILE="$(mktemp -t prefetch_sim_adaptive_capacity.XXXXXX)"
FILE_SIZE=$((512 * 1024 * 1024)) # 512MB
BLOCK_SIZE=4096
MIN_WINDOW=4
MAX_WINDOW=64
BURST_LEN=256
NUM_ACCESSES=8000
REPEATS=5
OUT_CSV=results/adaptive_capacity.csv

cleanup() {
    rm -f "$TEST_FILE"
}
trap cleanup EXIT

echo "burst_len,min_window,max_window,capacity_blocks,file_size,block_size,num_accesses,repeats,mean_elapsed_sec,stddev_elapsed_sec,mean_throughput_mb_s,mean_hit_rate" > "$OUT_CSV"

for capacity in 8 16 32 64 128; do
    "$BIN" "$TEST_FILE" "$FILE_SIZE" "$BLOCK_SIZE" "$capacity" "$MIN_WINDOW" "$MAX_WINDOW" "$BURST_LEN" "$NUM_ACCESSES" "$REPEATS" >> "$OUT_CSV"
done

echo "results written to $OUT_CSV"
