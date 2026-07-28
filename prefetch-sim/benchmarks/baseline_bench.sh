#!/usr/bin/env bash
# Stage 0 baseline: builds baseline_bench, sweeps
# {sequential, random} x {page cache, O_DIRECT} against a single test file,
# and writes one CSV row per combination to results/baseline.csv.
#
# Expects build/baseline_bench to accept:
#   baseline_bench <file_path> <file_size_bytes> <block_size> <pattern> <direct> <repeats>
#     pattern: sequential | random
#     direct:  0 (page cache) | 1 (O_DIRECT)
#     repeats: number of timed read passes to average over
# and print one CSV line to stdout:
#   pattern,direct,file_size,block_size,repeats,mean_elapsed_sec,stddev_elapsed_sec,mean_throughput_mb_s
set -euo pipefail

cd "$(dirname "$0")/.."

make

BIN=build/baseline_bench
TEST_FILE="$(mktemp -t prefetch_sim_baseline.XXXXXX)"
FILE_SIZE=$((1024 * 1024 * 1024)) # 1GB
BLOCK_SIZE=4096
REPEATS=5
OUT_CSV=results/baseline.csv

cleanup() {
    rm -f "$TEST_FILE"
}
trap cleanup EXIT

echo "pattern,direct,file_size,block_size,repeats,mean_elapsed_sec,stddev_elapsed_sec,mean_throughput_mb_s" > "$OUT_CSV"

for pattern in sequential random; do
    for direct in 0 1; do
        "$BIN" "$TEST_FILE" "$FILE_SIZE" "$BLOCK_SIZE" "$pattern" "$direct" "$REPEATS" >> "$OUT_CSV"
    done
done

echo "results written to $OUT_CSV"
