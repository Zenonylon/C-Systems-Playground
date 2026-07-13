CC := gcc
CFLAGS := -Wall -Wextra -std=gnu11 -Iinclude
BUILD_DIR := build

SRC := src/malloc.c
TEST_SRC := tests/test_malloc.c
TEST_BIN := $(BUILD_DIR)/test_malloc

.PHONY: all test clean

all: $(BUILD_DIR)/malloc.o

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/malloc.o: $(SRC) include/malloc.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $(SRC) -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(SRC) $(TEST_SRC) include/malloc.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC) $(TEST_SRC) -o $(TEST_BIN)

clean:
	rm -rf $(BUILD_DIR)
