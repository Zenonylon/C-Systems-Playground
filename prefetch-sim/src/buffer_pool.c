#include "buffer_pool.h"
#include <stdio.h>
#include <stdlib.h>   // malloc, free

// BufferPool 자체는 main.c에서 선언해서 쓰는 형태로 한다.
void buffer_pool_init(BufferPool *pool, size_t capacity, size_t block_size) {
    /*
        init이 해야 하는 일 과정:
        1. slots 배열을 capacity만큼의 크기로 malloc(할당)한다.
        2. capacity, block_size 필드에 인자로 받은 값 저장한다.
        3. count, evict_pos, hits, misses를 전부 0으로 초기화한다.
    */
    pool->slots = malloc(capacity * sizeof(size_t));
    if (pool->slots == NULL) {
        fprintf(stderr, "buffer_pool_init: malloc failed\n");
        exit(EXIT_FAILURE);   // 여기서 프로그램 자체를 끝냄
    }

    pool->capacity = capacity;
    pool->block_size = block_size;
    pool->count = 0;
    pool->evict_pos = 0;
    pool->hits = 0;
    pool->misses = 0;
}

void buffer_pool_destroy(BufferPool *pool) {
    // free 후 NULL 설정 => 댕글링 포인터, 이중 해제 방지.
    free(pool->slots);
    pool->slots = NULL;
}

int buffer_pool_lookup(BufferPool *pool, size_t block_num) {
    /*
        해야 하는 일 과정:
        1. slots 배열에서 block_num이 있는지 찾기
        2. 찾았으면(hit) => hits 카운터 증가시키고 1 반환
        3. 못 찾았으면(miss) => misses 카운터 증가시키고 0 반환
    */
    for (size_t i=0; i<pool->count; i++) {
        // 찾은 경우
        if (pool->slots[i] == block_num) {
            pool->hits++;
            return 1;
        }
    }

    // 못 찾은 경우
    pool->misses++;
    return 0;
}

void buffer_pool_insert(BufferPool *pool, size_t block_num) {
    /*
        처리해야 할 세 가지 경우:
        1. 이미 있는 블록 → 아무것도 안 함 (통계 안 건드리는 별도 순회로 확인)
        2. 자리 있음 (count < capacity) → slots[count]에 넣고 count++
        3. 꽉 참 (count == capacity) → slots[evict_pos]에 덮어쓰고 evict_pos 갱신
    */

    // 1. 중복 확인 (lookup 쓰지 말 것! hits/misses 오염됨)
    for (size_t i = 0; i < pool->count; i++) {
        if (pool->slots[i] == block_num) {
            return;   // 이미 있으니 그냥 종료
        }
    }

    // 2. 자리가 있는 경우
    if (pool->count < pool->capacity) {
        // slots배열의 count번째 칸에 넣고, count를 1증가.
        pool->slots[pool->count] = block_num;
        pool->count++;
    }
    // 3. 꽉 찬 경우
    else {
        // slots의 evict_pos에 덮어쓰고, evict_pos를 1 증가시키고 배열 최대 크기로 나눈다.
        pool->slots[pool->evict_pos] = block_num;
        pool->evict_pos = (pool->evict_pos + 1) % pool->capacity;
    }
}

void buffer_pool_prefetch_range(BufferPool *pool, size_t start_block, size_t count) {
    /*
        처리해야 하는 일 과정:
        1. start_block부터 count개의 연속된 블록 번호를 순회한다.
           (start_block, start_block+1, ..., start_block + count - 1)
        2. 각 블록 번호를 buffer_pool_insert로 캐시에 넣는다.
           - insert가 중복 확인, 자리 유무, eviction까지 알아서 처리하므로
             여기서는 그냥 insert를 반복 호출하기만 하면 된다.
           - lookup과 달리 insert는 hits/misses 통계를 건드리지 않으므로
             prefetch 과정에서 통계가 오염될 걱정이 없다.
    */

    for (size_t i=0; i<count; i++) {
        buffer_pool_insert(pool, start_block + i);
    }

}

size_t buffer_pool_hits(BufferPool *pool) {     // 누적 hit 횟수를 반환한다.
    return pool->hits;
}    

size_t buffer_pool_misses(BufferPool *pool) {   // 누적 miss 횟수를 반환한다.
    return pool->misses;
}   