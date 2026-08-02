CC := gcc
CFLAGS := -Wall -Wextra -std=gnu11 -Iinclude
BUILD_DIR := build

SRC := src/buffer_pool.c src/readahead.c src/read_around.c src/workload_gen.c src/timer.c src/main.c
BIN := $(BUILD_DIR)/prefetch-sim

BASELINE_SRC := benchmarks/baseline_bench.c src/timer.c src/workload_gen.c
BASELINE_BIN := $(BUILD_DIR)/baseline_bench

.PHONY: all clean

all: $(BIN) $(BASELINE_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC) -lm -o $(BIN)

$(BASELINE_BIN): $(BASELINE_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(BASELINE_SRC) -lm -o $(BASELINE_BIN)

clean:
	rm -rf $(BUILD_DIR)
