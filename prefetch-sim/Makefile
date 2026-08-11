CC := gcc
CFLAGS := -Wall -Wextra -std=gnu11 -Iinclude
BUILD_DIR := build

SRC := src/buffer_pool.c src/lru_buffer_pool.c src/readahead.c src/read_around.c src/workload_gen.c src/timer.c src/main.c
BIN := $(BUILD_DIR)/prefetch-sim

BASELINE_SRC := benchmarks/baseline_bench.c src/timer.c src/workload_gen.c
BASELINE_BIN := $(BUILD_DIR)/baseline_bench

INTERRUPTED_SRC := benchmarks/interrupted_bench.c src/buffer_pool.c src/readahead.c src/timer.c
INTERRUPTED_BIN := $(BUILD_DIR)/interrupted_bench

ADAPTIVE_SRC := benchmarks/adaptive_bench.c src/buffer_pool.c src/adaptive_readahead.c src/timer.c
ADAPTIVE_BIN := $(BUILD_DIR)/adaptive_bench

MIXED_SRC := benchmarks/mixed_bench.c src/buffer_pool.c src/readahead.c src/adaptive_readahead.c src/timer.c
MIXED_BIN := $(BUILD_DIR)/mixed_bench

READ_AROUND_SRC := benchmarks/read_around_bench.c src/lru_buffer_pool.c src/read_around.c src/timer.c
READ_AROUND_BIN := $(BUILD_DIR)/read_around_bench

.PHONY: all clean

all: $(BIN) $(BASELINE_BIN) $(INTERRUPTED_BIN) $(ADAPTIVE_BIN) $(MIXED_BIN) $(READ_AROUND_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC) -lm -o $(BIN)

$(BASELINE_BIN): $(BASELINE_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(BASELINE_SRC) -lm -o $(BASELINE_BIN)

$(INTERRUPTED_BIN): $(INTERRUPTED_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INTERRUPTED_SRC) -lm -o $(INTERRUPTED_BIN)

$(ADAPTIVE_BIN): $(ADAPTIVE_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(ADAPTIVE_SRC) -lm -o $(ADAPTIVE_BIN)

$(MIXED_BIN): $(MIXED_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(MIXED_SRC) -lm -o $(MIXED_BIN)

$(READ_AROUND_BIN): $(READ_AROUND_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(READ_AROUND_SRC) -lm -o $(READ_AROUND_BIN)

clean:
	rm -rf $(BUILD_DIR)
