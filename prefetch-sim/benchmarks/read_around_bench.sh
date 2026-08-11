#!/usr/bin/env bash
# Stage 3: read-around vs. no prefetch (radius=0, plain LruBufferPool) on a
# workload that's randomly distributed but localized -- a bounded random
# walk (step size = jitter) confined to a single narrow region, re-visited
# many times so locality actually matters (unlike Stage 1/2's benchmarks,
# see docs/stage_progression.md). The walk (not i.i.d. uniform draws) is
# what gives read-around's neighbor-prefetch a real "next access lands near
# current access" pattern to exploit -- see the comment in
# read_around_bench.c's generate_locality_walk for why that distinction
# matters.
#
# capacity_blocks is kept smaller than region_width on purpose: if the cache
# could already hold the whole region, plain LRU alone saturates hit rate
# and read-around can only add prefetch overhead with nothing left to win
# (this is exactly what the first version of this benchmark measured, before
# generate_locality_walk replaced i.i.d. uniform draws -- see git history).
# With capacity < region_width, eviction pressure is real, so radius values
# at/above jitter should recover hit rate that plain LRU (radius=0) loses to
# eviction, while radius >> capacity should self-evict and fall back off
# (same FIFO/LRU-vs-window-size shape as interrupted_capacity_bench.sh).
#
# Expects build/read_around_bench to accept:
#   read_around_bench <file_path> <file_size_bytes> <block_size> <capacity_blocks> <region_width> <jitter> <radius> <num_accesses> <repeats>
# and print one CSV line to stdout:
#   region_width,jitter,radius,capacity_blocks,file_size,block_size,num_accesses,repeats,mean_elapsed_sec,stddev_elapsed_sec,mean_throughput_mb_s,mean_hit_rate
set -euo pipefail

cd "$(dirname "$0")/.."

make

BIN=build/read_around_bench
TEST_FILE="$(mktemp -t prefetch_sim_read_around.XXXXXX)"
FILE_SIZE=$((512 * 1024 * 1024)) # 512MB
BLOCK_SIZE=4096
CAPACITY_BLOCKS=64
REGION_WIDTH=512 # > capacity_blocks so plain LRU alone can't hold the whole region
JITTER=8
NUM_ACCESSES=8000
REPEATS=5
OUT_CSV=results/read_around.csv

cleanup() {
    rm -f "$TEST_FILE"
}
trap cleanup EXIT

echo "region_width,jitter,radius,capacity_blocks,file_size,block_size,num_accesses,repeats,mean_elapsed_sec,stddev_elapsed_sec,mean_throughput_mb_s,mean_hit_rate" > "$OUT_CSV"

for radius in 0 2 4 8 16 32; do
    "$BIN" "$TEST_FILE" "$FILE_SIZE" "$BLOCK_SIZE" "$CAPACITY_BLOCKS" "$REGION_WIDTH" "$JITTER" "$radius" "$NUM_ACCESSES" "$REPEATS" >> "$OUT_CSV"
done

echo "results written to $OUT_CSV"
