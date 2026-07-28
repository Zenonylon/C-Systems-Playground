#ifndef WORKLOAD_GEN_H
#define WORKLOAD_GEN_H

#include <stddef.h>

// 0이면 Sequential 접근, 1이면 Random 접근
typedef enum {
    PATTERN_SEQUENTIAL,
    PATTERN_RANDOM
} AccessPattern;


#define WORKLOAD_OK 0
#define WORKLOAD_ERR_INVALID_ARGS 1
#define WORKLOAD_ERR_NUM_BLOCKS_TOO_LARGE 2

// out_offsets: 호출자가 미리 malloc한 num_blocks 크기의 size_t 배열
// file_size, block_size로부터 접근 가능한 블록 수를 계산해 그 안에서 오프셋을 채움
// 성공 시 WORKLOAD_OK(0), 실패 시 위 에러 코드 중 하나 반환
int generate_offsets(AccessPattern pattern, size_t file_size, 
                      size_t block_size, size_t num_blocks, 
                      size_t *out_offsets);

#endif // WORKLOAD_GEN_H