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

    struct epoll_event read_event;
    read_event.events = EPOLLIN;  // | EPOLLET;
    read_event.data.ptr = co_routine;
    int ret = epoll_ctl(co_routine->co_scheduler->co_epoll, EPOLL_CTL_ADD, co_routine->co_id, &read_event);
    if (ret == -1) goto error_epoll_add;

    return co_routine;


error_epoll_add:
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
    if (!co_routine) return;
    if (co_routine->co_id != -1) {
        epoll_ctl(co_routine->co_scheduler->co_epoll, EPOLL_CTL_DEL, co_routine->co_id, 0);
        close(co_routine->co_id);
    }
    free(co_routine);
}

int CoTimeout(CoRoutine *co_routine, int64_t timeout) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    Heap *heap = *(Heap **)co_routine->co_scheduler->co_ktimeoutd->co_closure;
    int64_t timestamp = timeout + ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return heap_push(heap, timestamp, co_routine);
}

/** END: coroutine level api **/


/** BEGIN: scheduler level api **/

static void CoKLoopD(int ptr_high_bits, int ptr_low_bits) {
    CoRoutine *co_kloopd = CoGet(ptr_high_bits, ptr_low_bits);
    CoScheduler *co_scheduler = co_kloopd->co_scheduler;

    // launch init coroutine
    uint64_t count = 1;
    write(co_scheduler->co_init->co_id, &count, sizeof(count));

    int go = 1;
    int64_t wait_time = -1;
    Heap *heap = *(Heap **)co_scheduler->co_ktimeoutd->co_closure;
    struct epoll_event epoll_events[EPOLL_SIZE];
    while (go) {
        wait_time = -1;
        if (heap_size(heap)) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            wait_time = heap_top(heap)->key - ts.tv_sec * 1000 - ts.tv_nsec / 1000000;
        }

        int num_events = epoll_wait(co_scheduler->co_epoll, epoll_events, EPOLL_SIZE, wait_time);

        if (heap_size(heap)) {
            swapcontext(&co_scheduler->co_kloopd->co_context, &co_scheduler->co_ktimeoutd->co_context);
        }

        for (int i = 0; i < num_events; i++) {
            CoRoutine *co_routine = (CoRoutine *)epoll_events[i].data.ptr;
            
            // clear event
            read(co_routine->co_id, &count, sizeof(count));
            
            if (co_routine->co_id == co_kloopd->co_id) {
                go = 0;
                break;
            }

            co_routine->event_type = EVENT_NOTIFY;
            swapcontext(&co_kloopd->co_context, &co_routine->co_context);
        }
    }
}

static void CoKTimeoutD(int ptr_high_bits, int ptr_low_bits) {
    CoRoutine *co_ktimeoutd = CoGet(ptr_high_bits, ptr_low_bits);
    CoRoutine *co_kloopd = co_ktimeoutd->co_scheduler->co_kloopd;
    Heap *heap = *(Heap **)co_ktimeoutd->co_closure;

    struct timespec ts;
    int64_t timestamp;
    CoRoutine *co_routine;
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
        timestamp = ts.tv_sec * 1000 + ts.tv_nsec / 1000000 + 10;
        while (heap_size(heap) && heap_top(heap)->key <= timestamp) {
            co_routine = (CoRoutine *)heap_top(heap)->value;
            co_routine->event_type = EVENT_TIMEOUT;
            swapcontext(&co_ktimeoutd->co_context, &co_routine->co_context);
            heap_pop(heap);
        }
        swapcontext(&co_ktimeoutd->co_context, &co_kloopd->co_context);
    }
}

CoScheduler *CoInit(void (*co_init)(int, int)) {
    CoScheduler *co_scheduler = (CoScheduler *)malloc(sizeof(CoScheduler));
    if (!co_scheduler) goto error;
    
    co_scheduler->co_epoll = epoll_create(EPOLL_SIZE);
    if (co_scheduler->co_epoll == -1) goto error_epoll;

    co_scheduler->co_origin.co_scheduler = co_scheduler;
    getcontext(&co_scheduler->co_origin.co_context);

    co_scheduler->co_kloopd = CoCreate(&co_scheduler->co_origin, CoKLoopD, 0);
    if (!co_scheduler->co_kloopd) goto error_co_kloopd;

    co_scheduler->co_ktimeoutd = CoCreate(co_scheduler->co_kloopd, CoKTimeoutD, sizeof(Heap *));
    if (!co_scheduler->co_ktimeoutd) goto error_co_ktimeoutd;
    Heap *heap = heap_create();
    if (!heap) goto error_co_ktimeoutd_heap;
    *(Heap **)co_scheduler->co_ktimeoutd->co_closure = heap;

    co_scheduler->co_init = CoCreate(co_scheduler->co_kloopd, co_init, 0);
    if (!co_scheduler->co_init) goto error_co_init;

    return co_scheduler;


error_co_init:
    heap_destroy(heap);
error_co_ktimeoutd_heap:
    CoDestroy(co_scheduler->co_ktimeoutd);
error_co_ktimeoutd:
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
    free(*(Heap **)co_scheduler->co_ktimeoutd->co_closure);
    CoDestroy(co_scheduler->co_ktimeoutd);
    CoDestroy(co_scheduler->co_kloopd);
    close(co_scheduler->co_epoll);
    free(co_scheduler);
}

void CoExit(CoScheduler *co_scheduler) {
    uint64_t count = 1;
    write(co_scheduler->co_kloopd->co_id, &count, sizeof(count));
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

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("[%s] timeout %ld:%ld\n", __FUNCTION__, ts.tv_sec, ts.tv_nsec);
    { // CoSleep(1000)
        CoTimeout(co_sub1, 1000);
        swapcontext(&co_sub1->co_context, &co_sub1->co_scheduler->co_kloopd->co_context);
    }
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("[%s] timeout %ld:%ld\n", __FUNCTION__, ts.tv_sec, ts.tv_nsec);

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