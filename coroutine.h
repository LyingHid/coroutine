#include <stdint.h>
#include <ucontext.h>


#define CO_STACK_SIZE (2*1024*1024)


typedef struct __GloveCoRoutine {
    struct __GloveCoScheduler *co_scheduler;
    int co_id;
    unsigned char co_stack[CO_STACK_SIZE];
    ucontext_t co_context;
    struct epoll_event *epoll_event;
    unsigned char co_closure[];
} CoRoutine;

typedef struct __GloveCoScheduler {
    int co_epoll;
    ucontext_t origin_context;
    CoRoutine *co_init;
    CoRoutine co_os;
} CoScheduler;


#define CoGet(ptr_high_bits, ptr_low_bits) \
({ uintptr_t ptr = ((uintptr_t)ptr_high_bits << 32) | (uintptr_t)ptr_low_bits << 32 >> 32; (CoRoutine *)ptr; })


CoRoutine *CoCreate(CoScheduler *co_scheduler, CoRoutine *co_main, void (*fn)(int, int), size_t co_closure);
void CoResume(CoRoutine *swap_out, CoRoutine *swap_in);
void CoYield(CoRoutine *swap_out);
void CoDestroy(CoRoutine *co_routine);

CoScheduler *CoInit(void (*co_init)(int, int));
void CoRun(CoScheduler *co_scheduler);
void CoExit(CoScheduler *co_scheduler);