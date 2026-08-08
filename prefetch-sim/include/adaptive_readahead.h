#ifndef ADAPTIVE_READAHEAD_H
#define ADAPTIVE_READAHEAD_H

#include <stddef.h>
#include "buffer_pool.h"

/*
    적응형(Adaptive) Read-Ahead 방식.
    순차 접근이 이어지는 동안 윈도우 크기를 배수로 키우고(min→max 범위 내),
    순차 흐름이 깨지면 윈도우를 min_window로 리셋한다.
    고정 윈도우(readahead.h)와 달리 window가 실시간으로 변한다.
*/

typedef struct {
    BufferPool *pool;

    size_t window;       // 현재 윈도우 크기 (실시간으로 변함)
    size_t min_window;   // 윈도우 하한 (시작값 / 리셋 시 돌아갈 값)
    size_t max_window;   // 윈도우 상한 (capacity 초과로 인한 캐시 오염 방지)

    size_t last_block;   // 직전에 접근한 블록 번호
    int have_last;       // last_block이 유효한지

    size_t readahead_edge; // 아직 미리 읽어오지 않은 첫 블록 번호
    int have_edge;         // readahead_edge가 유효한지
} AdaptiveReadahead;

// min_window로 시작하고, max_window를 상한으로 설정
void adaptive_readahead_init(AdaptiveReadahead *ra, BufferPool *pool,
                             size_t min_window, size_t max_window);

// 매 블록 접근마다 호출. 순차성 판단 + 윈도우 조절 + prefetch 결정
void adaptive_readahead_on_access(AdaptiveReadahead *ra, size_t block_num);

#endif // ADAPTIVE_READAHEAD_H