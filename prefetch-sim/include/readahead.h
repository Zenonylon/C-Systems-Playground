/*
    "readahead 모듈의 역할"
    이 모듈은 "언제 어떤 블록을 미리 읽어올 것인가"를 결정하는 역할을 담당한다.
*/

#ifndef READAHEAD_H
#define READAHEAD_H

#include <stddef.h>
#include "buffer_pool.h"

/*
    고정 크기(Fixed-window) Read-Ahead 방식이다. 순차 접근이 계속되는 동안, 
    현재 접근 위치가 이전에 미리 읽어 둔 영역의 끝에 도달하면 
    다음 window개의 블록을 한 번에 미리 읽어 온다. 
    순차적이지 않은 접근이 발생하면 현재의 순차 접근 상태를 초기화한다.
*/

typedef struct {
    BufferPool *pool;   // 포인터 방식: buffer_pool은 밖에 따로 있고, readahead는 그걸 가리키기만 함. 즉, 둘의 생명주기가 다르다.
    size_t window;      // 순차 접근이 감지됐을 때 한 번에 미리 읽어올 블록 갯수

    size_t last_block;  // 바로 직전에 접근했던 블록 번호.
    int have_last;      // last_block이 아직 유효한 값인지 나타내는 플래그 (첫 접근 시점엔 직전 블록 이라는 것이 없으니.)

    size_t readahead_edge; // 아직 미리 읽어오지 않은 첫 번째 블록 번호
    int have_edge;         // readahead_edge가 아직 유효한지 나타내는 플래그 (아직 한 번도 prefetch를 안 걸었으면 edge라는 개념 자체가 없으니)
} Readahead;

void readahead_init(Readahead *ra, BufferPool *pool, size_t window);

// 매번 블록에 접근할 때마다 한 번씩 호출하되, buffer_pool_lookup으로 조회하고 필요하면 buffer_pool_insert한 뒤에 호출한다.
void readahead_on_access(Readahead *ra, size_t block_num);
#endif // READAHEAD_H

