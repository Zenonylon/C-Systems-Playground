#include "read_around.h"
 
void read_around_init(ReadAround *ra, LruBufferPool *pool,
                      size_t radius, size_t total_blocks) {
    ra->pool = pool;
    ra->radius = radius;
    ra->total_blocks = total_blocks;
}
 
void read_around_on_access(ReadAround *ra, size_t block_num) {
    // block_num을 중심으로 [block_num - radius, block_num + radius]를 채운다.
 
    // ── 아래쪽 경계(start) 계산 ──
    // block_num - radius가 음수(underflow)가 되지 않도록 clamp.
    // size_t는 부호 없음이라 그냥 빼면 0 아래로 내려갈 때 거대한 값이 되므로,
    // "radius가 block_num보다 크면 0부터 시작" 으로 방어한다.
    size_t start;
    if (block_num > ra->radius) {
        start = block_num - ra->radius;
    } else {
        start = 0;
    }
 
    // ── 위쪽 경계(end) 계산 ──
    // block_num + radius가 파일 마지막 블록을 넘지 않도록 clamp.
    size_t end = block_num + ra->radius;
    if (ra->total_blocks > 0 && end > ra->total_blocks - 1) {
        end = ra->total_blocks - 1;
    }
 
    // ── [start, end] 구간을 한 번에 prefetch ──
    // prefetch_range는 (시작 블록, 개수)를 받으므로 개수 = end - start + 1.
    size_t count = end - start + 1;
    lru_buffer_pool_prefetch_range(ra->pool, start, count);
}
 