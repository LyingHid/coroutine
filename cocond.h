#ifndef __HEADER_GLOVE_COCOND__
#define __HEADER_GLOVE_COCOND__


#include "coroutine.h"
#include "utils/list.h"


typedef struct __glove_co_cond_timeout {
    co_timeout_t  co_timeout;
    list_t       *node;
} co_cond_timeout_t;

typedef struct __glove_co_cond_waiter {
    list_t        node;
    co_routine_t *co_routine;
    co_timeout_t *co_timeout;
} co_cond_waiter_t;

typedef struct __glove_co_cond {
    int                  cond_id;
    list_t               cond_waiters;
    co_event_listener_t  co_event_listener;
    co_scheduler_t      *co_scheduler;
} co_cond_t;


co_cond_t *co_cond_init(co_cond_t *co_cond, co_routine_t *co_routine);
void co_cond_destroy(co_cond_t *co_cond);
int co_cond_wait(co_cond_t *co_cond, co_routine_t *co_routine, int64_t timeout);
void co_cond_signal_n(co_cond_t *co_cond, co_routine_t *co_routine, int64_t n);
void co_cond_signal_n_ext(co_cond_t *co_cond, int64_t n);


#endif