#include "../include/timer.h"

// 측정 시작 시점의 시각을 &t->start에 기록
void timer_start(Timer *t) {
    clock_gettime(CLOCK_MONOTONIC, &t->start);
}

// 측정 종료 시점의 시각을 &t->end에 기록
void timer_stop(Timer *t) {
    clock_gettime(CLOCK_MONOTONIC, &t->end);
}

// start와 end의 차이를 double타입으로 반환
double timer_elapsed_sec(Timer *t) {
    return (t->end.tv_sec - t->start.tv_sec) +
           (t->end.tv_nsec - t->start.tv_nsec) / 1e9;
}
