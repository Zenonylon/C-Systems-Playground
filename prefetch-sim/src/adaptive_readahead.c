#include "adaptive_readahead.h"

void adaptive_readahead_init(AdaptiveReadahead *ra, BufferPool *pool,
                             size_t min_window, size_t max_window) {
    ra->pool = pool;
    ra->window = min_window;       // 가장 보수적인 값으로 시작
    ra->min_window = min_window;
    ra->max_window = max_window;
    ra->last_block = 0;
    ra->have_last = 0;
    ra->readahead_edge = 0;
    ra->have_edge = 0;
}

void adaptive_readahead_on_access(AdaptiveReadahead *ra, size_t block_num) {
    // ── 1단계: 순차 접근 판단 (Stage 1과 동일) ──
    int sequential = ra->have_last && (block_num == ra->last_block + 1);

    if (sequential && ra->max_window > 0) {
        // ── 2단계: 순차적이다. prefetch 여부 판단 ──

        if (!ra->have_edge) {
            // [case A] 순차 접근이 막 시작된 시점.
            // 첫 prefetch이므로 현재 window(= min_window)로 미리 읽는다.
            buffer_pool_prefetch_range(ra->pool, block_num + 1, ra->window);
            ra->readahead_edge = block_num + 1 + ra->window;
            ra->have_edge = 1;

        } else if (block_num + 1 == ra->readahead_edge) {
            // [case B] 커서가 미리 읽어둔 마지막 블록에 도달한 시점.
            // 순차 흐름이 "믿을 만하다"는 신호이므로, 다음 prefetch를 걸기
            // 직전에 window를 키운다 (배수 증가, max_window로 clamp).
            //   → 실제로 다음 윈도우를 당길 때만 키우므로,
            //     미리 읽어둔 걸 소비하는 중에는 불필요하게 커지지 않음.
            ra->window *= 2;
            if (ra->window > ra->max_window) {
                ra->window = ra->max_window;
            }

            // 커진 window로 다음 구간을 미리 읽고, edge를 그만큼 전진.
            buffer_pool_prefetch_range(ra->pool, ra->readahead_edge, ra->window);
            ra->readahead_edge += ra->window;
        }
        // (그 외: 순차적이지만 아직 edge 미도달 → 미리 읽어둔 영역 소비 중.
        //  아무 것도 하지 않는다. Stage 1과 동일.)

    } else {
        // ── 3단계: 순차적이지 않다 (random 점프 또는 첫 접근) ──
        // 순차 흐름이 깨졌으니 edge를 무효화하고,
        // window를 가장 보수적인 min_window로 리셋한다.
        //   → 다음에 다시 순차 접근이 시작되면 case A부터 작은 window로 재시작.
        ra->have_edge = 0;
        ra->window = ra->min_window;
    }

    // ── 4단계: 마무리 (항상 실행, Stage 1과 동일) ──
    ra->last_block = block_num;
    ra->have_last = 1;
}