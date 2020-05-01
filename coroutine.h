#ifndef __HEADER_GLOVE_COROUTINE__
#define __HEADER_GLOVE_COROUTINE__


#include <stdint.h>
#include <ucontext.h>


#define CO_STACK_SIZE (128 * 1024)


typedef struct __glove_co_event_listener {
    void (*callback)(struct __glove_co_event_listener *);
} co_event_listener_t;


typedef struct __glove_co_routine {
    int                          eventfd;
    unsigned char                co_stack[CO_STACK_SIZE];
    ucontext_t                   co_context;
    co_event_listener_t          co_event_listener;
    struct __glove_co_scheduler *co_scheduler;
} co_routine_t;

typedef struct __glove_co_scheduler {
    int           co_running;
    int           epollfd;
    ucontext_t    ctx_origin;
    co_routine_t  co_kloopd;
    co_routine_t  co_uinit;
} co_scheduler_t;


/** from linux/kernel.h
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 */
#define container_of(ptr, type, member) ({				      \
	void *__mptr = (void *)(ptr);					          \
	((type *)((intptr_t)__mptr - __builtin_offsetof(type, member))); })

static inline co_routine_t *co_this(int ptr_high_bits, int ptr_low_bits) {
    uintptr_t ptr = ((uintptr_t)ptr_high_bits << 32) | (uintptr_t)ptr_low_bits << 32 >> 32;
    return (co_routine_t *)ptr;
}


co_routine_t *co_init(co_routine_t *co_routine, co_scheduler_t *co_scheduler, void (*fn)(int, int));
void co_destroy(co_routine_t *co_routine);
void co_resume(co_routine_t *swap_in);
void co_yield(co_routine_t *swap_out);


co_scheduler_t *co_scheduler_init(co_scheduler_t *co_scheduler, void (*uinit)(int, int));
void co_scheduler_run(co_scheduler_t *co_scheduler);
void co_scheduler_exit(co_scheduler_t *co_scheduler);


#endif