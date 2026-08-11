#include "lru_buffer_pool.h"
#include <stdio.h>
#include <stdlib.h>

void lru_buffer_pool_init(LruBufferPool *pool, size_t capacity, size_t block_size) {
    pool->slots = malloc(capacity * sizeof(size_t));
    pool->last_used = malloc(capacity * sizeof(size_t));
    if (pool->slots == NULL || pool->last_used == NULL) {
        fprintf(stderr, "lru_buffer_pool_init: malloc failed\n");
        exit(EXIT_FAILURE);
    }

    pool->capacity = capacity;
    pool->block_size = block_size;
    pool->count = 0;
    pool->clock = 0;
    pool->hits = 0;
    pool->misses = 0;
}

void lru_buffer_pool_destroy(LruBufferPool *pool) {
    free(pool->slots);
    free(pool->last_used);
    pool->slots = NULL;
    pool->last_used = NULL;
}

int lru_buffer_pool_lookup(LruBufferPool *pool, size_t block_num) {
    for (size_t i = 0; i < pool->count; i++) {
        if (pool->slots[i] == block_num) {
            pool->hits++;
            pool->clock++;
            pool->last_used[i] = pool->clock;
            return 1;
        }
    }

    pool->misses++;
    return 0;
}

void lru_buffer_pool_insert(LruBufferPool *pool, size_t block_num) {
    // 1. 이미 있는 블록이면 재삽입하지 않되 recency는 갱신한다.
    for (size_t i = 0; i < pool->count; i++) {
        if (pool->slots[i] == block_num) {
            pool->clock++;
            pool->last_used[i] = pool->clock;
            return;
        }
    }

    // 2. 자리가 있는 경우
    if (pool->count < pool->capacity) {
        pool->clock++;
        pool->slots[pool->count] = block_num;
        pool->last_used[pool->count] = pool->clock;
        pool->count++;
        return;
    }

    // 3. 꽉 찬 경우: last_used가 가장 작은(가장 오래 안 쓰인) slot을 찾아 덮어쓴다.
    size_t victim = 0;
    for (size_t i = 1; i < pool->count; i++) {
        if (pool->last_used[i] < pool->last_used[victim]) {
            victim = i;
        }
    }

    pool->clock++;
    pool->slots[victim] = block_num;
    pool->last_used[victim] = pool->clock;
}

void lru_buffer_pool_prefetch_range(LruBufferPool *pool, size_t start_block, size_t count) {
    for (size_t i = 0; i < count; i++) {
        lru_buffer_pool_insert(pool, start_block + i);
    }
}

size_t lru_buffer_pool_hits(LruBufferPool *pool) {
    return pool->hits;
}

size_t lru_buffer_pool_misses(LruBufferPool *pool) {
    return pool->misses;
}
