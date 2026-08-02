/*
    buffer pool이 해야 하는 일
    1. 조회: "블록 N이 지금 캐시에 있는가?" 확인
    2. 저장: 새 블록 데이터를 캐시에 넣기
    3. 꽉 찼을 때 처리: 자리가 없으면 뭔가를 밀어내야 함. (eviction)
*/

/*
    ifndef는 정의 돼 있지 않으면 발생하는 전처리기로,
    buffer_pool.h를 처음 실행할 땐 'BUFFER_POOL_H'라는 이름이 없어서 #define BUFFER_POOL_H를 실행하고 파일 내용을 읽는다.
    그 후에 이 파일을 실행한다면, BUFFER_POOL_H가 존재하기에, ifndef에 의해 중복 실행을 하지 않게 돼, "error: redefinition of 'BufferPool'"와 같은
    모든 변수(구조체, enum, 전역 변수) 오류가 발생하지 않게 됨.
    중복 방지 차원에서 해당 구문을 쓰는 게 '오류 차단'이라는 효과를 볼 수 있으니 쓰는 게 정답이다. 
*/ 
#ifndef BUFFER_POOL_H   
#define BUFFER_POOL_H

#include <stddef.h>     // size_t, NULL, ptrdiff_t, offsetof 등을 제공함.

typedef struct {
    size_t capacity;     // 최대 담을 블록 수
    size_t block_size;   // 블록 하나의 크기
    size_t *slots;       // 담긴 블록 번호 배열
    size_t count;        // 현재 차 있는 개수
    size_t evict_pos;    // FIFO eviction 위치
    size_t hits;
    size_t misses;
} BufferPool;

// 캐시를 초기화한다.
// capacity 크기의 slots 배열을 할당하고 count/evict_pos/hits/misses를 0으로 세팅한다.
// buffer_pool_destroy와 반드시 짝을 이뤄 호출해야 한다.
void buffer_pool_init(BufferPool *pool, size_t capacity, size_t block_size);

// 캐시를 해제한다.
// init에서 할당한 slots 배열의 메모리를 free한다.
void buffer_pool_destroy(BufferPool *pool);

// 블록 block_num이 현재 캐시에 있는지 조회한다.
// 있으면 hit으로 간주해 1을 반환하고, 없으면 miss로 간주해 0을 반환한다.
// 내부적으로 hits 또는 misses 카운터를 1 증가시킨다.
int buffer_pool_lookup(BufferPool *pool, size_t block_num);

// 블록 block_num을 캐시에 넣는다.
// 캐시가 꽉 찬 경우 FIFO 정책에 따라 한 블록을 밀어내고 자리를 만든다.
// 이미 존재하는 블록이면 중복 삽입하지 않는다.
void buffer_pool_insert(BufferPool *pool, size_t block_num);

// start_block부터 count개의 연속된 블록을 캐시에 미리 채운다 (readahead용).
// 내부적으로 buffer_pool_insert를 count번 반복 호출한다.
void buffer_pool_prefetch_range(BufferPool *pool, size_t start_block, size_t count);


// 통계 조회
size_t buffer_pool_hits(BufferPool *pool);      // 누적 hit 횟수를 반환한다.
size_t buffer_pool_misses(BufferPool *pool);    // 누적 miss 횟수를 반환한다.


#endif // BUFFER_POOL_H
