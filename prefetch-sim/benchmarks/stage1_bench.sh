#!/usr/bin/env bash
# Stage 1: fixed-window read-ahead vs. no read-ahead (window=0), on top of
# the hand-rolled buffer_pool, for sequential and random access patterns.
# Sweeps window size for sequential (where it should help) and spot-checks
# random (where it shouldn't) against a single O_DIRECT-backed test file,
# writing one CSV row per combination to results/stage1.csv.
#
# Expects build/prefetch-sim to accept:
#   prefetch-sim <file_path> <file_size_bytes> <block_size> <pattern> <capacity_blocks> <window> <repeats>
#     pattern:         sequential | random
#     capacity_blocks: buffer_pool capacity in blocks (must be >= window)
#     window:          read-ahead window in blocks (0 disables read-ahead)
#     repeats:         number of timed access passes to average over
# and print one CSV line to stdout:
#   pattern,window,capacity_blocks,file_size,block_size,repeats,mean_elapsed_sec,stddev_elapsed_sec,mean_throughput_mb_s,mean_hit_rate
set -euo pipefail

cd "$(dirname "$0")/.."

make

BIN=build/prefetch-sim
TEST_FILE="$(mktemp -t prefetch_sim_stage1.XXXXXX)"
FILE_SIZE=$((512 * 1024 * 1024)) # 512MB
BLOCK_SIZE=4096
CAPACITY_BLOCKS=128 # must be >= largest window swept below
REPEATS=5
OUT_CSV=results/stage1.csv

cleanup() {
    rm -f "$TEST_FILE"
}
trap cleanup EXIT

echo "pattern,window,capacity_blocks,file_size,block_size,repeats,mean_elapsed_sec,stddev_elapsed_sec,mean_throughput_mb_s,mean_hit_rate" > "$OUT_CSV"

for window in 0 4 16 64; do
    "$BIN" "$TEST_FILE" "$FILE_SIZE" "$BLOCK_SIZE" sequential "$CAPACITY_BLOCKS" "$window" "$REPEATS" >> "$OUT_CSV"
done

# Random only needs a couple of points -- read-ahead has nothing to trigger
# on, so window shouldn't move the numbers.
for window in 0 64; do
    "$BIN" "$TEST_FILE" "$FILE_SIZE" "$BLOCK_SIZE" random "$CAPACITY_BLOCKS" "$window" "$REPEATS" >> "$OUT_CSV"
done

echo "results written to $OUT_CSV"
