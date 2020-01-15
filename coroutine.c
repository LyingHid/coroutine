#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include "utils/heap.h"
#include "coroutine.h"


#define EPOLL_SIZE 1024

/** from linux/kernel.h
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 */
#define container_of(ptr, type, member) ({				      \
	void *__mptr = (void *)(ptr);					          \
	((type *)((intptr_t)__mptr - __builtin_offsetof(type, member))); })

#define type_ptr_cast(ptr, type) (*(type **)ptr)
#define type_array_cast(ptr, type) ((type **)ptr)


static int64_t get_timestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void co_timeout_callback(CoEventListener *self) {
    CoRoutine *co_routine = self->co_routine;
    swapcontext(&co_routine->co_scheduler->co_kloopd->co_context, &co_routine->co_context);
    free(self);
}

static void co_resume_callback(CoEventListener *self) {
    CoRoutine *co_routine = self->co_routine;

    // clear event
    uint64_t count;
    read(co_routine->co_id, &count, sizeof(count));

    co_routine->event_type = EVENT_RESUME;
    swapcontext(&co_routine->co_scheduler->co_kloopd->co_context, &co_routine->co_context);
}


/** BEGIN: coroutine level api **/

CoRoutine *CoCreate(CoRoutine *co_return, void (*fn)(int, int), size_t co_closure) {
    CoRoutine *co_routine = (CoRoutine *)malloc(sizeof(CoRoutine) + co_closure);
    if (!co_routine) goto error;

    co_routine->co_scheduler = co_return->co_scheduler;

    co_routine->co_id = eventfd(0, EFD_NONBLOCK);
    if (co_routine->co_id == -1) goto error_co_id;

    getcontext(&co_routine->co_context);
    co_routine->co_context.uc_link = &co_return->co_context;
    co_routine->co_context.uc_stack.ss_sp = co_routine->co_stack;
    co_routine->co_context.uc_stack.ss_size = CO_STACK_SIZE;
    int ptr_high_bits = (uintptr_t)co_routine >> 32;
    int ptr_low_bits = (uintptr_t)co_routine << 32 >> 32;
    makecontext(&co_routine->co_context, (void (*)(void))fn, 2, ptr_high_bits, ptr_low_bits);

    co_routine->co_event_listener = malloc(sizeof(CoEventListener));
    if (!co_routine->co_event_listener) goto error_co_event_listener;
    co_routine->co_event_listener->callback = co_resume_callback;
    co_routine->co_event_listener->co_routine = co_routine;

    struct epoll_event read_event;
    read_event.events = EPOLLIN;  // | EPOLLET;
    read_event.data.ptr = co_routine->co_event_listener;
    int ret = epoll_ctl(co_routine->co_scheduler->co_epoll, EPOLL_CTL_ADD, co_routine->co_id, &read_event);
    if (ret == -1) goto error_epoll_add;

    return co_routine;


error_epoll_add:
    free(co_routine->co_event_listener);
error_co_event_listener:
    close(co_routine->co_id);
error_co_id:
    free(co_routine);
error:
    return 0;
}

void CoResume(CoRoutine *swap_out, CoRoutine *swap_in) {
    uint64_t count = 1;
    write(swap_in->co_id, &count, sizeof(count));
    swapcontext(&swap_out->co_context, &swap_in->co_scheduler->co_kloopd->co_context);
}

void CoYield(CoRoutine *swap_out) {
    ucontext_t *co_context = swap_out->co_context.uc_link;
    CoRoutine *swap_in = container_of(co_context, CoRoutine, co_context);
    CoResume(swap_out, swap_in);
}

void CoDestroy(CoRoutine *co_routine) {
    epoll_ctl(co_routine->co_scheduler->co_epoll, EPOLL_CTL_DEL, co_routine->co_id, 0);
    free(co_routine->co_event_listener);
    close(co_routine->co_id);
    free(co_routine);
}

uintptr_t CoTimeout(CoRoutine *co_routine, int64_t timeout) {
    CoEventListener *co_event_listener = malloc(sizeof(CoEventListener));
    if (!co_event_listener) goto error_malloc;
    co_event_listener->callback = co_timeout_callback;
    co_event_listener->co_routine = co_routine;

    Heap *heap = type_ptr_cast(co_routine->co_scheduler->co_kloopd->co_closure, Heap);
    int64_t timestamp = timeout + get_timestamp();
    int ret = heap_push(heap, timestamp, co_event_listener);
    if (ret) goto error_heap_push;

    return (uintptr_t)co_event_listener;


error_heap_push:
    free(co_event_listener);
error_malloc:
    return -1;
}

/** END: coroutine level api **/


/** BEGIN: scheduler level api **/

static void CoKLoopD(int ptr_high_bits, int ptr_low_bits) {
    CoRoutine *co_kloopd = CoGet(ptr_high_bits, ptr_low_bits);
    CoScheduler *co_scheduler = co_kloopd->co_scheduler;

    {
        // launch init coroutine
        uint64_t count = 1;
        write(co_scheduler->co_init->co_id, &count, sizeof(count));
    }

    int64_t timestamp, wait_time;
    Heap *heap = type_ptr_cast(co_scheduler->co_kloopd->co_closure, Heap);
    struct epoll_event epoll_events[EPOLL_SIZE];
    while (co_scheduler->go) {
        timestamp = get_timestamp();
        wait_time = heap_size(heap) ? heap_top(heap)->key - timestamp : -1;
        int num_events = epoll_wait(co_scheduler->co_epoll, epoll_events, EPOLL_SIZE, wait_time);

        for (timestamp = get_timestamp() + 10; heap_size(heap) && heap_top(heap)->key < timestamp; heap_pop(heap)) {
            CoEventListener *co_event_listener = (CoEventListener *)heap_top(heap)->value;
            co_event_listener->callback(co_event_listener);
        }

        for (int i = 0; i < num_events; i++) {
            CoEventListener *co_event_listener = (CoEventListener *)epoll_events[i].data.ptr;
            co_event_listener->callback(co_event_listener);
        }
    }
}

CoScheduler *CoInit(void (*co_init)(int, int)) {
    CoScheduler *co_scheduler = (CoScheduler *)malloc(sizeof(CoScheduler));
    if (!co_scheduler) goto error;
    
    co_scheduler->co_epoll = epoll_create(EPOLL_SIZE);
    if (co_scheduler->co_epoll == -1) goto error_epoll;

    co_scheduler->go = 1;

    co_scheduler->co_origin.co_scheduler = co_scheduler;
    getcontext(&co_scheduler->co_origin.co_context);

    co_scheduler->co_kloopd = CoCreate(&co_scheduler->co_origin, CoKLoopD, sizeof(Heap *) * 2);
    if (!co_scheduler->co_kloopd) goto error_co_kloopd;
    // for timeout
    type_array_cast(co_scheduler->co_kloopd->co_closure, Heap)[0] = heap_create();
    if (!type_array_cast(co_scheduler->co_kloopd->co_closure, Heap)[0]) goto event_co_kloopd_heap0;
    // for timeout cancel
    type_array_cast(co_scheduler->co_kloopd->co_closure, Heap)[1] = heap_create();
    if (!type_array_cast(co_scheduler->co_kloopd->co_closure, Heap)[1]) goto event_co_kloopd_heap1;

    co_scheduler->co_init = CoCreate(co_scheduler->co_kloopd, co_init, 0);
    if (!co_scheduler->co_init) goto error_co_init;

    return co_scheduler;


error_co_init:
    heap_destroy(type_array_cast(co_scheduler->co_kloopd->co_closure, Heap)[1]);
event_co_kloopd_heap1:
    heap_destroy(type_array_cast(co_scheduler->co_kloopd->co_closure, Heap)[0]);
event_co_kloopd_heap0:
    CoDestroy(co_scheduler->co_kloopd);
error_co_kloopd:
    close(co_scheduler->co_epoll);
error_epoll:
    free(co_scheduler);
error:
    return 0;
}

void CoRun(CoScheduler *co_scheduler) {
    swapcontext(&co_scheduler->co_origin.co_context, &co_scheduler->co_kloopd->co_context);

    CoDestroy(co_scheduler->co_init);
    //TODO: fix mem leak if we have co_event_listener in the heap
    heap_destroy(type_array_cast(co_scheduler->co_kloopd->co_closure, Heap)[1]);
    heap_destroy(type_array_cast(co_scheduler->co_kloopd->co_closure, Heap)[0]);
    CoDestroy(co_scheduler->co_kloopd);
    close(co_scheduler->co_epoll);
    free(co_scheduler);
}

void CoExit(CoScheduler *co_scheduler) {
    co_scheduler->go = 0;
}

/** END: scheduler level api **/


/** BEGIN: unit test **/

#ifdef __MODULE_COROUTINE__
// gcc -Wall -g -D__MODULE_COROUTINE__ coroutine.c utils/heap.c #-fsanitize=address

#include <stdio.h>


void sub1(int ptr_high_bits, int ptr_low_bits) {
    printf("[%s] enter\n", __FUNCTION__);
    CoRoutine *co_sub1 = CoGet(ptr_high_bits, ptr_low_bits);

    printf("[%s] co_sub1 addr: %#lX\n", __FUNCTION__, (uintptr_t)co_sub1);

    *(int *)co_sub1->co_closure = 13579;
    printf("[%s] yield\n", __FUNCTION__);
    CoYield(co_sub1);
    *(int *)co_sub1->co_closure = 24680;

    printf("[%s] timeout %ld\n", __FUNCTION__, get_timestamp());
    { // CoSleep(1000)
        CoTimeout(co_sub1, 1000);
        swapcontext(&co_sub1->co_context, &co_sub1->co_scheduler->co_kloopd->co_context);
    }
    printf("[%s] timeout %ld\n", __FUNCTION__, get_timestamp());

    printf("[%s] return\n", __FUNCTION__);
}

void sub2(int ptr_high_bits, int ptr_low_bits) {
    CoRoutine *co_sub2 = CoGet(ptr_high_bits, ptr_low_bits);
    printf("[%s] enter\n", __FUNCTION__);
    
    printf("[%s] co_sub2 addr: %#lX\n", __FUNCTION__, (uintptr_t)co_sub2);

    CoRoutine *co_sub1 = CoCreate(co_sub2, sub1, sizeof(int));
    printf("[%s] co_sub1 addr: %#lX\n", __FUNCTION__, (uintptr_t)co_sub1);
    CoResume(co_sub2, co_sub1);
    printf("[%s] co_sub1 closure: %d\n", __FUNCTION__, *(int *)co_sub1->co_closure);
    CoResume(co_sub2, co_sub1);
    printf("[%s] co_sub1 closure: %d\n", __FUNCTION__, *(int *)co_sub1->co_closure);
    CoDestroy(co_sub1);

    printf("[%s] return\n", __FUNCTION__);
}

void init(int ptr_high_bits, int ptr_low_bits) {
    CoRoutine *co_init = (CoRoutine *)CoGet(ptr_high_bits, ptr_low_bits);
    printf("[%s] enter\n", __FUNCTION__);

    printf("[%s] Hello Coroutine\n", __FUNCTION__);

    CoRoutine *co_sub2 = CoCreate(co_init, sub2, 0);
    printf("[%s] co_sub2 addr: %#lX\n", __FUNCTION__, (uintptr_t)co_sub2);
    CoResume(co_init, co_sub2);
    CoDestroy(co_sub2);

    CoExit(co_init->co_scheduler);

    printf("[%s] return\n", __FUNCTION__);
}

int main(int argc, char *argv[]) {
    CoScheduler *co_scheduler = CoInit(init);
    if (!co_scheduler) {
        printf("error init env\n");
        return -1;
    }
    CoRun(co_scheduler);
    return 0;
}


#endif

/** END: unit test **/