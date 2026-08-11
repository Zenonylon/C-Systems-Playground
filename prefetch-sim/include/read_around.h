#ifndef READ_AROUND_H
#define READ_AROUND_H
 
#include <stddef.h>
#include "lru_buffer_pool.h"

/*
    Read-Around 방식.
    블록 N에 접근하면 그 "주변"(N-radius ~ N+radius)을 함께 미리 읽어온다.
    순차(read-ahead)와 달리 앞뒤 양방향을 채우므로,
    국소적으로 몰린 random 접근(좁은 범위에서 튀는 패턴)에 유리하다.
    파일 경계(맨 앞/맨 끝)를 벗어나는 블록은 건너뛴다.

    Stage 1/2의 BufferPool(FIFO)과 달리 LruBufferPool을 쓴다 — read-around는
    같은 지역을 반복 방문하는 워크로드를 겨냥하므로, "가장 오래전에 넣은 것"이
    아니라 "가장 최근에 안 쓰인 것"을 밀어내는 게 이 전략의 가정과 맞는다.
*/

typedef struct {
    LruBufferPool *pool;
    size_t radius;        // 접근한 블록의 앞뒤로 각각 몇 개를 당길지
    size_t total_blocks;  // 파일 전체 블록 수 (경계 처리에 사용)
} ReadAround;

// radius: 앞뒤 반경, total_blocks: 파일의 전체 블록 수(경계 clamp용)
void read_around_init(ReadAround *ra, LruBufferPool *pool,
                      size_t radius, size_t total_blocks);
 
// 매 블록 접근마다 호출. block_num 주변 [N-radius, N+radius]를 캐시에 채운다.
void read_around_on_access(ReadAround *ra, size_t block_num);
 
#endif // READ_AROUND_H
 