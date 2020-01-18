#ifndef __HEADER_GLOVE_COROUTINE__
#define __HEADER_GLOVE_COROUTINE__


#include <stdint.h>
#include <time.h>
#include "cotype.h"


static inline int64_t get_timestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}


static inline co_routine_t *co_get(int ptr_high_bits, int ptr_low_bits) {
    uintptr_t ptr = ((uintptr_t)ptr_high_bits << 32) | (uintptr_t)ptr_low_bits << 32 >> 32;
    return (co_routine_t *)ptr;
}

co_routine_t *co_init(co_routine_t *co_routine, co_routine_t *co_return, void (*fn)(int, int));
void co_destroy(co_routine_t *co_routine);
void co_resume(co_routine_t *swap_out, co_routine_t *swap_in);
void co_yield(co_routine_t *swap_out);

co_scheduler_t *co_scheduler_init(co_scheduler_t *co_scheduler, void (*co_init)(int, int));
void co_scheduler_run(co_scheduler_t *co_scheduler);
void co_scheduler_exit(co_scheduler_t *co_scheduler);


#endif