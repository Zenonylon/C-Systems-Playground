# Simulating Read-Ahead and Read-Around Caching Strategies in C: A Comparative Study

A userspace simulator that compares fixed/adaptive read-ahead and read-around
block caching strategies against sequential, random, and strided I/O workloads,
built to understand how the Linux page cache decides what to prefetch and why.

## Problem Definition

Disk and SSD I/O latency is dominated by per-request overhead (seek time on
spinning disks, queueing and command overhead on NVMe/SSDs), not by the raw
bytes transferred. Workloads that touch data with spatial locality — reading
a file sequentially, or repeatedly around a hot region — pay that per-request
cost far more often than necessary if the kernel (or an application-level
cache) fetches only what was explicitly requested.

Read-ahead and read-around are two strategies for exploiting that locality:

- **Read-ahead**: after detecting a sequential access pattern, prefetch the
  next N blocks before they're requested, so the actual request becomes a
  cache hit.
- **Read-around**: when a block is requested, also pull in a window of
  neighboring blocks, on the assumption that nearby data is likely to be
  accessed soon (useful for random-but-clustered access, e.g. page faults).

This project asks: for a given access pattern (sequential / random /
strided), which strategy — and which tuning of it (fixed-size vs. adaptive
window) — actually improves hit rate and throughput, and by how much? The
goal is to make that tradeoff visible and measurable rather than assumed.

## Scope

This project covers **Stage 0–3**:

- **Stage 0 — Baseline measurement**: benchmark `read()` on a large
  (~1GB) file, sequential vs. random access, with and without `O_DIRECT`
  (bypassing the page cache), to make the cost of a cache miss — and the
  value readahead can recover — concrete before building anything.
- **Stage 1 — Fixed-window read-ahead**: a hand-rolled buffer pool; on a
  read of block N, prefetch N+1, N+2, … into the cache and measure the
  speedup on sequential access.
- **Stage 2 — Adaptive read-ahead**: grow the read-ahead window while
  sequential hits continue and shrink it on a miss, mirroring how the
  Linux kernel's `blk-mq`/readahead path adapts window size to observed
  access behavior.
- **Stage 3 — Read-around cache**: an LRU-evicted cache that, on a miss,
  also fills the K blocks around the requested block — tested against a
  workload that's randomly distributed but localized (e.g. random access
  within a narrow range).

**Stretch, documented but not required for this iteration:**
- **Stage 4** — sequential / strided / random / zipfian (hot-block-biased)
  workloads compared head-to-head across all strategies, visualized as
  hit-rate/throughput graphs.
- **Stage 5** — comparing this simulator's behavior against the real
  kernel's `mm/readahead.c`.

## Architecture

```
prefetch-sim/
├── include/
│   ├── buffer_pool.h       # block cache public API
│   ├── readahead.h         # fixed/adaptive read-ahead public API
│   ├── read_around.h       # read-around cache public API
│   ├── workload_gen.h      # workload generator public API
│   └── timer.h             # timing utility, shared by Stage 0-4 benchmarks
├── src/
│   ├── buffer_pool.c       # fixed-capacity map of block# -> buffer,
│   │                       # eviction policy, hit/miss accounting (Stage 0-3 common)
│   ├── readahead.c         # sequential-access detection + fixed/adaptive prefetch
│   ├── read_around.c       # on-miss window fetch around the requested block
│   ├── workload_gen.c      # sequential/random offset generation (Stage 0),
│   │                       # extended with strided/zipfian later (Stage 4)
│   ├── timer.c             # clock_gettime(CLOCK_MONOTONIC)-based timing
│   └── main.c              # wires a workload + strategy together, emits stats
├── benchmarks/
│   ├── baseline_bench.c    # Stage 0: raw read()/O_DIRECT vs page-cache I/O,
│   │                       # no buffer_pool/readahead involved — stays as the
│   │                       # baseline Stage 1-3 strategies are compared against
│   └── run_experiments.sh  # sweeps workload x strategy x parameters, writes CSVs
├── results/
│   └── *.csv, graphs       # raw experiment output (generated, not hand-written)
└── README.md
```

### Request flow

```
workload_gen ──> block# request ──> buffer_pool.lookup()
                                        │
                              ┌─────────┴─────────┐
                              │                   │
                            HIT                 MISS
                              │                   │
                     return cached data     fetch block from
                              │             "storage" (simulated
                              │              latency), insert
                              │              into buffer_pool
                              │                   │
                              └─────────┬─────────┘
                                        │
                     strategy hook (readahead / read_around):
                     decide whether to prefetch additional
                     blocks now, based on the access pattern
                     seen so far
```

The buffer pool is strategy-agnostic — it only knows about hits, misses, and
eviction. `readahead` and `read_around` are pluggable hooks that observe each
request and decide what extra blocks (if any) to pull in ahead of time. This
mirrors the separation between the Linux page cache and its readahead logic.

## Planned Experiments

For each workload pattern (sequential, random, strided) and each strategy
(none / fixed read-ahead / adaptive read-ahead / read-around), measure:

- **Hit rate** — fraction of requests served without a simulated storage fetch.
- **Throughput** — simulated requests/sec given a configurable per-fetch latency.
- **Sensitivity** — how hit rate/throughput change as the read-ahead window
  or read-around radius is varied.

Results will be written to `results/*.csv` by `benchmarks/run_experiments.sh`
and rendered as bar charts (hit rate/throughput by pattern x strategy) and
line charts (hit rate vs. window size) in `results/`.

## Comparison to the Linux Kernel

<!-- TODO: once results are in, compare against how the real page cache /
     blk-mq behaves — where this simulator's simplifying assumptions
     (single-threaded, no I/O merging, fixed simulated latency) cause it to
     diverge from real kernel readahead (linux/mm/readahead.c) and the
     blk-mq request path. -->

## Build

```bash
make        # build the simulator
make clean  # remove build artifacts
```

## Project Structure

See [Architecture](#architecture) above.
