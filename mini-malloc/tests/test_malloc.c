#include "../include/malloc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// 지금 구현된 my_malloc / my_free 범위 안에서만 테스트한다.
// my_realloc / my_calloc은 아직 malloc.c에 정의되어 있지 않아 링크 에러가 나므로 제외.

static int tests_run = 0;
static int tests_failed = 0;

#define RUN(test) do { \
    tests_run++; \
    printf("[ RUN ] %s\n", #test); \
    if (test()) { \
        printf("[ OK  ] %s\n", #test); \
    } else { \
        printf("[FAIL ] %s\n", #test); \
        tests_failed++; \
    } \
} while (0)

// size 0을 요청하면 NULL을 반환해야 한다.
static int test_malloc_zero_returns_null(void)
{
    void *p = my_malloc(0);
    return p == NULL;
}

// 정상적인 크기를 요청하면 NULL이 아닌 포인터를 반환해야 한다.
static int test_malloc_returns_non_null(void)
{
    void *p = my_malloc(16);
    return p != NULL;
}

// 반환된 payload에 실제로 쓰고 읽을 수 있어야 한다 (헤더를 침범하지 않는지 확인).
static int test_malloc_write_read(void)
{
    char *p = my_malloc(64);
    if (!p)
        return 0;

    memset(p, 0xAB, 64);
    for (int i = 0; i < 64; i++) {
        if ((unsigned char)p[i] != 0xAB)
            return 0;
    }
    return 1;
}

// 연속으로 두 번 할당했을 때 두 블록의 payload가 겹치면 안 된다.
static int test_multiple_allocations_no_overlap(void)
{
    char *a = my_malloc(32);
    char *b = my_malloc(32);
    if (!a || !b)
        return 0;

    memset(a, 0x11, 32);
    memset(b, 0x22, 32);

    for (int i = 0; i < 32; i++) {
        if ((unsigned char)a[i] != 0x11 || (unsigned char)b[i] != 0x22)
            return 0;
    }
    return 1;
}

// 큰 크기를 여러 번 요청해도 매번 서로 다른 유효한 주소를 받아야 한다.
static int test_many_allocations_distinct(void)
{
    void *prev = NULL;
    for (int i = 0; i < 100; i++) {
        void *p = my_malloc(8);
        if (!p)
            return 0;
        if (p == prev)
            return 0;
        prev = p;
    }
    return 1;
}

// my_free는 아직 스텁(no-op)이지만, 최소한 호출했을 때 크래시가 나면 안 된다.
static int test_free_does_not_crash(void)
{
    void *p = my_malloc(16);
    if (!p)
        return 0;
    my_free(p);
    my_free(NULL);
    return 1;
}

int main(void)
{
    RUN(test_malloc_zero_returns_null);
    RUN(test_malloc_returns_non_null);
    RUN(test_malloc_write_read);
    RUN(test_multiple_allocations_no_overlap);
    RUN(test_many_allocations_distinct);
    RUN(test_free_does_not_crash);

    printf("\n%d/%d tests passed\n", tests_run - tests_failed, tests_run);
    return tests_failed == 0 ? 0 : 1;
}
