CC := gcc
CFLAGS := -Wall -Wextra -std=gnu11 -Iinclude
BUILD_DIR := build

SRC := src/buffer_pool.c src/readahead.c src/read_around.c src/workload_gen.c src/main.c
BIN := $(BUILD_DIR)/prefetch-sim

.PHONY: all clean

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC) -o $(BIN)

clean:
	rm -rf $(BUILD_DIR)
