#ifndef MINI_MALLOC_H   // 이 헤더가 여러 번 include돼도 중복 처리 안 되게 막는 가드
#define MINI_MALLOC_H

#include <stddef.h>     // size_t 타입이 여기에 정의돼 있음

// 함수 선언
void *my_malloc(size_t size);
void  my_free(void *ptr);
void *my_realloc(void *ptr, size_t size);
void *my_calloc(size_t nmemb, size_t size);

#endif
