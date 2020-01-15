#ifndef __HEADER_GLOVE_COROUTINE__
#define __HEADER_GLOVE_COROUTINE__


#include <stdint.h>
#include <errno.h>
#include <ucontext.h>


#define CO_STACK_SIZE (2*1024*1024)

#define EVENT_RESUME  0
#define EVENT_TIMEOUT 1


typedef struct __GloveCoRoutine {
    struct __GloveCoScheduler *co_scheduler;
    int co_id;
    int event_type;
    unsigned char co_stack[CO_STACK_SIZE];
    ucontext_t co_context;
    struct __GloveCoEventListener *co_event_listener;
    unsigned char co_closure[];
} CoRoutine;

typedef struct __GloveCoScheduler {
    int co_epoll;
    int go;
    CoRoutine *co_kloopd;
    CoRoutine *co_init;
    CoRoutine  co_origin;
} CoScheduler;

typedef struct __GloveCoEventListener {
    void (*callback) (struct __GloveCoEventListener *self);
    CoRoutine *co_routine;
    unsigned char data[];
} CoEventListener;


#define CoGet(ptr_high_bits, ptr_low_bits) \
({ uintptr_t ptr = ((uintptr_t)ptr_high_bits << 32) | (uintptr_t)ptr_low_bits << 32 >> 32; (CoRoutine *)ptr; })

CoRoutine *CoCreate(CoRoutine *co_return, void (*fn)(int, int), size_t co_closure);
void CoResume(CoRoutine *swap_out, CoRoutine *swap_in);
void CoYield(CoRoutine *swap_out);
void CoDestroy(CoRoutine *co_routine);
uintptr_t CoTimeout(CoRoutine *co_routine, int64_t timeout);

CoScheduler *CoInit(void (*co_init)(int, int));
void CoRun(CoScheduler *co_scheduler);
void CoExit(CoScheduler *co_scheduler);


#endif