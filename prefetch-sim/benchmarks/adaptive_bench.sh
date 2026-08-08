#!/usr/bin/env bash
# Stage 2 benchmark: sweeps burst_len x max_window (min_window held fixed and
# small) for the same interrupted trace used by interrupted_bench.sh, but
# driven through AdaptiveReadahead instead of the Stage 1 fixed-window
# Readahead.
#
# min_window is kept small (4) so every run starts conservative; max_window
# is what actually varies the ceiling the window can grow to during a long
# burst. capacity_blocks is kept tight (== largest max_window swept) so
# eviction pressure from an over-grown window still shows up, same rationale
# as interrupted_bench.sh.
#
# Expects build/adaptive_bench to accept:
#   adaptive_bench <file_path> <file_size_bytes> <block_size> <capacity_blocks> <min_window> <max_window> <burst_len> <num_accesses> <repeats>
# and print one CSV line to stdout:
#   burst_len,min_window,max_window,capacity_blocks,file_size,block_size,num_accesses,repeats,mean_elapsed_sec,stddev_elapsed_sec,mean_throughput_mb_s,mean_hit_rate
set -euo pipefail

cd "$(dirname "$0")/.."

make

BIN=build/adaptive_bench
TEST_FILE="$(mktemp -t prefetch_sim_adaptive.XXXXXX)"
FILE_SIZE=$((512 * 1024 * 1024)) # 512MB
BLOCK_SIZE=4096
MIN_WINDOW=4
NUM_ACCESSES=8000
REPEATS=5
OUT_CSV=results/adaptive.csv

cleanup() {
    rm -f "$TEST_FILE"
}
trap cleanup EXIT

echo "burst_len,min_window,max_window,capacity_blocks,file_size,block_size,num_accesses,repeats,mean_elapsed_sec,stddev_elapsed_sec,mean_throughput_mb_s,mean_hit_rate" > "$OUT_CSV"

for burst_len in 4 16 64 256; do
    for max_window in 4 16 64 256; do
        "$BIN" "$TEST_FILE" "$FILE_SIZE" "$BLOCK_SIZE" "$max_window" "$MIN_WINDOW" "$max_window" "$burst_len" "$NUM_ACCESSES" "$REPEATS" >> "$OUT_CSV"
    done
done

echo "results written to $OUT_CSV"
