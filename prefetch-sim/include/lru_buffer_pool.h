/*
    LruBufferPool: Stage 1/2가 쓰는 BufferPool(FIFO)과 독립적인, Stage 3
    read-around 전용 캐시. eviction 정책만 다르다 — capacity가 차면 "가장
    오래전에 넣은 것"이 아니라 "가장 최근에 안 쓰인 것"을 밀어낸다.

    FIFO는 evict_pos 하나만 회전시키면 됐지만, LRU는 "언제 마지막으로
    쓰였는지"를 slot마다 따로 기억해야 한다. 이 구현은 별도의 해시맵/연결
    리스트 없이, buffer_pool.c와 같은 "배열 + 선형 탐색" 스타일을 그대로
    유지한다 — slots[]와 나란히 last_used[]를 두고, eviction 때 그 배열에서
    최솟값 인덱스를 선형 탐색으로 찾는다. O(n)이지만 이 프로젝트 규모에서는
    충분하고, 기존 코드와 복잡도 수준이 맞는다.
*/
#ifndef LRU_BUFFER_POOL_H
#define LRU_BUFFER_POOL_H

#include <stddef.h>

typedef struct {
    size_t capacity;
    size_t block_size;
    size_t *slots;       // 담긴 블록 번호 배열
    size_t *last_used;   // slots[i]가 마지막으로 접근된 시각 (clock 값)
    size_t count;
    size_t clock;        // 접근할 때마다 증가하는 논리적 시계 (실제 시간 아님)
    size_t hits;
    size_t misses;
} LruBufferPool;

// slots/last_used를 capacity 크기로 할당하고 나머지 필드를 0으로 초기화한다.
// lru_buffer_pool_destroy와 반드시 짝을 이뤄야 한다.
void lru_buffer_pool_init(LruBufferPool *pool, size_t capacity, size_t block_size);

// slots/last_used를 free한다.
void lru_buffer_pool_destroy(LruBufferPool *pool);

// block_num이 캐시에 있는지 조회한다.
// hit이면 hits++ 하고 last_used를 현재 clock으로 갱신한 뒤 1을 반환한다
// (LRU는 "조회"도 접근이므로 recency를 갱신해야 FIFO와 의미가 달라진다).
// miss면 misses++ 하고 0을 반환한다.
int lru_buffer_pool_lookup(LruBufferPool *pool, size_t block_num);

// block_num을 캐시에 넣는다.
// 이미 있으면 재삽입하지 않되, recency는 갱신한다 (겹치는 read-around
// 윈도우가 같은 블록을 다시 prefetch할 때 "최근에 다시 쓰였다"는 신호를
// 잃지 않기 위함).
// 자리가 있으면 그냥 채우고, 꽉 찼으면 last_used가 가장 작은(=가장 오래
// 안 쓰인) slot을 선형 탐색으로 찾아 덮어쓴다.
void lru_buffer_pool_insert(LruBufferPool *pool, size_t block_num);

// start_block부터 count개의 연속된 블록을 lru_buffer_pool_insert로 채운다.
void lru_buffer_pool_prefetch_range(LruBufferPool *pool, size_t start_block, size_t count);

size_t lru_buffer_pool_hits(LruBufferPool *pool);
size_t lru_buffer_pool_misses(LruBufferPool *pool);

#endif // LRU_BUFFER_POOL_H
