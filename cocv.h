#ifndef __HEADER_GLOVE_COCV__
#define __HEADER_GLOVE_COCV__


#include "coroutine.h"
#include "utils/list.h"


typedef struct __glove_co_cv_timeout {
    int                  timerfd;
    int                  canceled;
    co_event_listener_t  co_event_listener;
    list_t              *node;
} co_cv_timeout_t;

typedef struct __glove_co_cv_waiter {
    list_t           node;
    co_routine_t    *co_routine;
    co_cv_timeout_t *co_cv_timeout;
} co_cv_waiter_t;

typedef struct __glove_co_cv {
    int                  eventfd;
    list_t               cv_waiters;
    co_event_listener_t  co_event_listener;
    co_scheduler_t      *co_scheduler;
} co_cv_t;


co_cv_t *co_cv_init(co_cv_t *co_cv, co_scheduler_t *co_scheduler);
void co_cv_destroy(co_cv_t *co_cv);
int co_cv_wait(co_cv_t *co_cv, co_routine_t *co_routine, int64_t wait_ms);
void co_cv_signal(co_cv_t *co_cv, int64_t n);


#endif