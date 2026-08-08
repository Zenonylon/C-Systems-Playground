#!/usr/bin/env bash
# Stage 2 motivation probe, part 2: tests the pollution hypothesis that
# interrupted_bench.sh (burst_len x window, capacity fixed >= window) could
# not -- capacity_blocks < window, so a single prefetch_range call can evict
# its own earlier entries (or other still-needed blocks) before the burst
# consumes them.
#
# window and burst_len are held fixed (burst_len long enough that, if fully
# retained, it would consume the whole prefetched window); capacity_blocks
# sweeps from well below window to well above it.
#
# Expects build/interrupted_bench to accept:
#   interrupted_bench <file_path> <file_size_bytes> <block_size> <capacity_blocks> <window> <burst_len> <num_accesses> <repeats>
# and print one CSV line to stdout:
#   burst_len,window,capacity_blocks,file_size,block_size,num_accesses,repeats,mean_elapsed_sec,stddev_elapsed_sec,mean_throughput_mb_s,mean_hit_rate
set -euo pipefail

cd "$(dirname "$0")/.."

make

BIN=build/interrupted_bench
TEST_FILE="$(mktemp -t prefetch_sim_interrupted_capacity.XXXXXX)"
FILE_SIZE=$((512 * 1024 * 1024)) # 512MB
BLOCK_SIZE=4096
WINDOW=64
BURST_LEN=256
NUM_ACCESSES=8000
REPEATS=5
OUT_CSV=results/interrupted_capacity.csv

cleanup() {
    rm -f "$TEST_FILE"
}
trap cleanup EXIT

echo "burst_len,window,capacity_blocks,file_size,block_size,num_accesses,repeats,mean_elapsed_sec,stddev_elapsed_sec,mean_throughput_mb_s,mean_hit_rate" > "$OUT_CSV"

for capacity in 8 16 32 64 128; do
    "$BIN" "$TEST_FILE" "$FILE_SIZE" "$BLOCK_SIZE" "$capacity" "$WINDOW" "$BURST_LEN" "$NUM_ACCESSES" "$REPEATS" >> "$OUT_CSV"
done

echo "results written to $OUT_CSV"
