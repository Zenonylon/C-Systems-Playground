#include "workload_gen.h"
#include <stdlib.h>
#include <time.h>

// 부분 Fisher-Yates: array[0..n-1] 중 앞 k개 자리에
// 무작위로 뽑힌 값들이 채워지도록 k번만 반복
static void partial_shuffle(size_t *array, size_t n, size_t k) {
    for (size_t i = 0; i < k; i++) {
        // 아직 확정 안 된 범위 [i, n-1] 중에서 무작위로 하나 선택
        size_t j = i + rand() % (n - i);
        size_t tmp = array[i];
        array[i] = array[j];
        array[j] = tmp;
    }
}

int generate_offsets(AccessPattern pattern, size_t file_size,
                     size_t block_size, size_t num_blocks,
                     size_t *out_offsets)
{
    if (out_offsets == NULL || block_size == 0) {
        return WORKLOAD_ERR_INVALID_ARGS;
    }

    size_t total_blocks = file_size / block_size;

    if (num_blocks > total_blocks) {
        return WORKLOAD_ERR_NUM_BLOCKS_TOO_LARGE;
    }

    static int seeded = 0;
    if (!seeded) {
        srand((unsigned)time(NULL));
        seeded = 1;
    }

    // 전체 블록 인덱스 후보 배열 (0 ~ total_blocks-1)
    size_t *candidates = malloc(total_blocks * sizeof(size_t));
    if (candidates == NULL) {
        return WORKLOAD_ERR_INVALID_ARGS;
    }
    for (size_t i = 0; i < total_blocks; i++) {
        candidates[i] = i;
    }

    // 앞 num_blocks개 자리에 무작위로 num_blocks개를 뽑아 채움 (비복원 추출)
    partial_shuffle(candidates, total_blocks, num_blocks);

    if (pattern == PATTERN_SEQUENTIAL) {
        // 뽑힌 num_blocks개를 오름차순 정렬 후 오프셋으로 변환
        // (개수가 많아지면 qsort로 교체 권장 — 지금은 버블 정렬로 충분)
        for (size_t i = 0; i < num_blocks - 1; i++) {
            for (size_t j = 0; j < num_blocks - 1 - i; j++) {
                if (candidates[j] > candidates[j + 1]) {
                    size_t tmp = candidates[j];
                    candidates[j] = candidates[j + 1];
                    candidates[j + 1] = tmp;
                }
            }
        }
    }
    // PATTERN_RANDOM이면 partial_shuffle로 이미 섞인 순서 그대로 사용

    for (size_t i = 0; i < num_blocks; i++) {
        out_offsets[i] = candidates[i] * block_size;
    }

    free(candidates);
    return WORKLOAD_OK;
}