#include "../include/malloc.h"
#include <unistd.h>
#include <string.h>

// void *는 주소를 저장하는 타입이다. 다만, int, char, double 등등 중에서 무엇인지 모르기에 void로 나타낸 것.

/*
메모리를 할당
메모리를 회수
공간 배정
*/

typedef struct block {
    size_t size;
    int free;
} block_t;

// 메모리를 받아서 Header를 만들고 Payload를 반환한다.
void *my_malloc(size_t size)
{
    // 1. 필요한 메모리 크기를 계산한다.
    if (size == 0)
        return NULL;

    size_t total_size = sizeof(block_t) + size;

    // 2. OS에게 메모리를 요청한다.
    block_t *block = sbrk(total_size);

    if (block == (void *)-1)    // sbrk() 실패 검사
        return NULL;

    // 3. Header를 초기화한다.
    block->size = total_size;
    block->free = 0;

    // 4. Payload 주소를 반환한다.
    return (void *)(block + 1);
}

void my_free(void *ptr) 
{

}

