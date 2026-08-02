#include "readahead.h"
#include <stdbool.h>

void readahead_init(Readahead *ra, BufferPool *pool, size_t window) {
    ra->pool = pool;
    ra->window = window;
    ra->last_block = 0;
    ra->have_last = 0;
    ra->readahead_edge = 0;
    ra->have_edge = 0;
}

// 매번 블록에 접근할 때마다 한 번씩 호출하되, buffer_pool_lookup으로 조회하고 필요하면 buffer_pool_insert한 뒤에 호출한다.
void readahead_on_access(Readahead *ra, size_t block_num) {
    // ── 1단계: 지금 접근이 "순차적"인가? ──
    // 순차적이려면 두 조건이 모두 참이어야 한다:
    //   (1) have_last: 비교할 직전 블록 기록이 실제로 존재해야 함
    //       (첫 접근이면 직전 블록이 없으니 순차 판단 자체가 불가능)
    //   (2) block_num == last_block + 1: 지금 블록이 직전 블록 바로 다음
    int sequential = ra->have_last && (block_num == ra->last_block + 1);

    if (sequential && ra->window > 0) {
        // ── 2단계: 순차적이다. 그럼 지금 prefetch를 걸어야 하나? ──

        if (!ra->have_edge) {
            // [case A] 순차 접근이 "막 시작된" 시점.
            // 아직 미리 읽어둔 영역이 없으니(have_edge 거짓),
            // block_num 다음부터 window개를 한 번에 미리 읽어온다.
            buffer_pool_prefetch_range(ra->pool, block_num + 1, ra->window);

            // 미리 읽어둔 영역의 "끝 다음 블록"을 edge로 기록.
            // 예: block_num=10, window=4 이면 11,12,13,14를 읽었으니 edge=15.
            ra->readahead_edge = block_num + 1 + ra->window;
            ra->have_edge = 1;

        } else if (block_num + 1 == ra->readahead_edge) {
            // [case B] 이미 prefetch를 걸어둔 상태이고,
            // 커서가 미리 읽어둔 "마지막 블록"에 막 도달한 시점.
            // (block_num+1 == edge 라는 건, 지금 읽는 block_num이
            //  캐시에 남은 마지막 미리읽기 블록이라는 뜻)
            // → 곧 캐시가 소진되니, edge부터 다음 window개를 또 당겨온다.
            buffer_pool_prefetch_range(ra->pool, ra->readahead_edge, ra->window);

            // edge를 그만큼 앞으로 전진시킨다.
            ra->readahead_edge += ra->window;
        }
        // (그 외: 순차적이지만 아직 edge에 도달하지 않았다면
        //  미리 읽어둔 영역을 소비하는 중이므로 아무것도 하지 않는다.
        //  매 블록마다 prefetch를 걸면 같은 범위를 중복해서 읽게 되므로,
        //  edge에 도달했을 때만 거는 것이 핵심.)

    } else {
        // ── 3단계: 순차적이지 않다 (random 점프 또는 첫 접근). ──
        // 순차 흐름이 끊겼으니 edge 상태를 무효화한다.
        // 다음에 다시 순차 접근이 시작되면 case A부터 새로 시작.
        ra->have_edge = 0;
    }

    // ── 4단계: 마무리 (항상 실행) ──
    // 이번 접근을 "직전 블록"으로 기록해서, 다음 호출의 순차 판단에 쓴다.
    ra->last_block = block_num;
    ra->have_last = 1;
}