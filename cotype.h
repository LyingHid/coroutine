#ifndef __HEADER_GLOVE_COTYPE__
#define __HEADER_GLOVE_COTYPE__


#include <errno.h>
#include <ucontext.h>


#define EOK 0

#define CO_STACK_SIZE (128 * 1024)


typedef struct __glove_co_event_listener {
    void (*callback)(struct __glove_co_event_listener *);
} co_event_listener_t;

typedef struct __glove_co_timeout {
    int                 canceled;
    int64_t             heap_key;
    co_event_listener_t co_event_listener;
} co_timeout_t;

typedef struct __glove_co_routine {
    int                          co_id;
    unsigned char                co_stack[CO_STACK_SIZE];
    ucontext_t                   co_context;
    co_event_listener_t          co_event_listener;
    struct __glove_co_scheduler *co_scheduler;
} co_routine_t;

typedef struct __glove_co_scheduler {
    int          go;
    int          co_epoll;
    co_routine_t co_origin;
    co_routine_t co_kloopd;
    co_routine_t co_uinit;
} co_scheduler_t;


#endif