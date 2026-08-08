#!/usr/bin/env bash
# Head-to-head: sweeps fixed_window while min_window/max_window (Stage 2)
# stay fixed at a single "auto" config, so each row replays the SAME mixed
# trace (burst lengths drawn per-burst from {4,16,64,256}, see
# mixed_bench.c) through both strategies.
#
# The story this targets: interrupted_bench.sh / adaptive_bench.sh each
# sweep ONE fixed burst_len per run, so a Stage 1 window tuned for that run
# never gets to look bad. Here burst length varies within a single run, so
# no single fixed_window can be right for all of it -- Stage 2's one config
# should hold up across the fixed_window sweep while Stage 1 only wins near
# its own value and loses everywhere else.
#
# capacity_blocks == max_window (both 64) so Stage 2's own growth never
# exceeds capacity and evicts its own not-yet-consumed prefetch -- that
# self-pollution (same mechanism as interrupted_capacity_bench.sh) swamps
# everything else if max_window is allowed past capacity, and isn't the
# comparison this script is after. fixed_window is swept past capacity on
# purpose: an oversized *static* window overshooting is exactly the failure
# mode Stage 2 is meant to avoid.
#
# Expects build/mixed_bench to accept:
#   mixed_bench <file_path> <file_size_bytes> <block_size> <capacity_blocks> <fixed_window> <min_window> <max_window> <num_accesses> <repeats>
# and print one CSV line to stdout:
#   capacity_blocks,fixed_window,min_window,max_window,file_size,block_size,num_accesses,repeats,stage1_mean_elapsed_sec,stage1_stddev_elapsed_sec,stage1_mean_throughput_mb_s,stage1_mean_hit_rate,stage2_mean_elapsed_sec,stage2_stddev_elapsed_sec,stage2_mean_throughput_mb_s,stage2_mean_hit_rate
set -euo pipefail

cd "$(dirname "$0")/.."

make

BIN=build/mixed_bench
TEST_FILE="$(mktemp -t prefetch_sim_mixed.XXXXXX)"
FILE_SIZE=$((512 * 1024 * 1024)) # 512MB
BLOCK_SIZE=4096
CAPACITY_BLOCKS=64
MIN_WINDOW=4
MAX_WINDOW=64
NUM_ACCESSES=8000
REPEATS=5
OUT_CSV=results/mixed.csv

cleanup() {
    rm -f "$TEST_FILE"
}
trap cleanup EXIT

echo "capacity_blocks,fixed_window,min_window,max_window,file_size,block_size,num_accesses,repeats,stage1_mean_elapsed_sec,stage1_stddev_elapsed_sec,stage1_mean_throughput_mb_s,stage1_mean_hit_rate,stage2_mean_elapsed_sec,stage2_stddev_elapsed_sec,stage2_mean_throughput_mb_s,stage2_mean_hit_rate" > "$OUT_CSV"

for fixed_window in 4 16 64 256; do
    "$BIN" "$TEST_FILE" "$FILE_SIZE" "$BLOCK_SIZE" "$CAPACITY_BLOCKS" "$fixed_window" "$MIN_WINDOW" "$MAX_WINDOW" "$NUM_ACCESSES" "$REPEATS" >> "$OUT_CSV"
done

echo "results written to $OUT_CSV"
