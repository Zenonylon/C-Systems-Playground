#include "../include/malloc.h"
#include <unistd.h>
#include <string.h>
#include <stdint.h>

// void *는 특정 자료형을 지정하지 않은 포인터이다. 따라서 어떤 타입의 주소든 저장 가능하지만, 역참조하려면 반드시 원래 타입으로 형변환해야 한다.
// 64비트 환경에서의 주소의 크기는 자료형과 관계없이 동일하게 8Byte(=64bit)이다.

/*
메모리를 할당
메모리를 회수
공간 배정
*/

/*  <block 구조체 설명>
    멤버 크기의 합은 64비트 환경에서 28B이지만,
    컴파일러가 메모리 정렬(alignment)을 맞추기 위해
    4B의 padding을 추가하여 sizeof(block_t)는 32B가 된다.
*/
typedef struct block {  // 헤더 역할
    size_t size;    // 8B
    int free;    // 4B
    struct block *prev;    // 8B    
    struct block *next;    // 8B
} block_t;

/*  free list   */
static block_t *free_list = NULL;

static block_t *find_free_block(size_t size);
static void remove_free_block(block_t *block);

/* 역할: 메모리를 받아서 Header를 만들고 Payload를 반환한다. */
void *my_malloc(size_t size)
{
    // 1. 필요한 메모리 크기를 계산한다.
    if (size == 0)
        return NULL;

    // size가 너무 커서 sizeof(block_t)를 더했을 때 오버플로가 나면
    // total_size가 실제 요청보다 훨씬 작은 값으로 랩어라운드된다.
    if (size > SIZE_MAX - sizeof(block_t))
        return NULL;

    // 2. first-fit으로 free_list에서 쓸 수 있는 block을 찾는다.
    block_t *block = find_free_block(size);

    if (block != NULL)
    {
        remove_free_block(block);
        block->free = 0;

        return (void *)(block + 1);
    }

    // 3. free_list를 못 쓰는 경우, OS에게 메모리를 요청한다.
    size_t total_size = sizeof(block_t) + size;

    block = sbrk(total_size);

    if (block == (void *)-1)    // sbrk() 실패 검사
        return NULL;

    // 4. Header를 초기화한다.
    block->size = size;
    block->free = 0;

    // 5. Payload 주소를 반환한다.
    return (void *)(block + 1);
}

/* 역할: 할당됐던 메모리의 Header의 free를 1로 설정하고 free_list에 추가한다. */
void my_free(void *ptr) 
{
    if (ptr == NULL)
        return;

    // 1. Header를 찾는다.
    block_t *block = (block_t *)ptr - 1;

    // 이미 free된 block을 다시 free하면 free_list가 자기 자신을 가리키게 되어
    // 리스트가 깨지므로, 더블 프리는 무시한다.
    if (block->free)
        return;

    // 2. free = 1로 설정한다.
    block->free = 1;

    // 3. free list에 삽입한다.
    block->prev = NULL;
    block->next = free_list;

    if (free_list != NULL)
        free_list->prev = block;

    free_list = block;
}

// first-fit으로 구현함.
static block_t *find_free_block(size_t size)
{
    block_t *current = free_list;

    while (current != NULL)
    {
        if (current->free && current->size >= size)
            return current;

        current = current->next;
    }

    return NULL;
}

static void remove_free_block(block_t *block)
{
    if (block->prev)
        block->prev->next = block->next;
    else
        free_list = block->next;

    if (block->next)
        block->next->prev = block->prev;

    block->prev = NULL;
    block->next = NULL;
}
