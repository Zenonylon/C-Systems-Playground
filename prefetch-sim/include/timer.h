#ifndef TIMER_H
#define TIMER_H

#include <time.h>

typedef struct { struct timespec start, end; } Timer;
void timer_start(Timer *t);
void timer_stop(Timer *t);
double timer_elapsed_sec(Timer *t);

#endif // TIMER_H
