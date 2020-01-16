#include <stddef.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include "cointernal.h"
#include "coroutine.h"
#include "utils/heap.h"


static const size_t EPOLL_SIZE = 1024;


static void co_routine_fdevent_callback(co_event_listener_t *co_event_listener) {
    co_routine_t *co_routine = container_of(co_event_listener, co_routine_t, co_event_listener);

    // clear event
    uint64_t count;
    read(co_routine->co_id, &count, sizeof(count));

    swapcontext(&co_routine->co_scheduler->co_kloopd.co_context, &co_routine->co_context);
}


static void kloopd(int ptr_high_bits, int ptr_low_bits) {
    co_routine_t *co_kloopd = co_get(ptr_high_bits, ptr_low_bits);
    co_scheduler_t *co_scheduler = co_kloopd->co_scheduler;

    int64_t timestamp, wait_time;
    heap_t *heap = get_kloopd_heap(co_kloopd);
    struct epoll_event epoll_events[EPOLL_SIZE];
    while (co_scheduler->go) {
        timestamp = get_timestamp();
        wait_time = heap_size(heap) ? *heap_top(heap) - timestamp : -1;
        int num_events = epoll_wait(co_scheduler->co_epoll, epoll_events, EPOLL_SIZE, wait_time);

        for (timestamp = get_timestamp() + 10; heap_size(heap) && *heap_top(heap) < timestamp; heap_pop(heap)) {
            int64_t *heap_key = heap_top(heap);
            co_timeout_t *co_timeout = container_of(heap_key, co_timeout_t, heap_key);
            co_event_listener_t *co_event_listener = &co_timeout->co_event_listener;
            co_event_listener->callback(co_event_listener);
        }

        for (int i = 0; i < num_events; i++) {
            co_event_listener_t *co_event_listener = (co_event_listener_t *)epoll_events[i].data.ptr;
            co_event_listener->callback(co_event_listener);
        }
    }
}


co_routine_t *co_init(co_routine_t *co_routine, co_routine_t *co_return, void (*fn)(int, int)) {
    co_routine->co_id = eventfd(0, EFD_NONBLOCK);
    if (co_routine->co_id == -1) goto error_co_id;

    getcontext(&co_routine->co_context);
    co_routine->co_context.uc_link = &co_return->co_context;
    co_routine->co_context.uc_stack.ss_sp = co_routine->co_stack;
    co_routine->co_context.uc_stack.ss_size = CO_STACK_SIZE;
    int ptr_high_bits = (uintptr_t)co_routine >> 32;
    int ptr_low_bits = (uintptr_t)co_routine << 32 >> 32;
    makecontext(&co_routine->co_context, (void (*)(void))fn, 2, ptr_high_bits, ptr_low_bits);

    co_routine->co_event_listener.callback = co_routine_fdevent_callback;

    co_routine->co_scheduler = co_return->co_scheduler;

    struct epoll_event read_event;
    read_event.events = EPOLLIN;  // | EPOLLET;
    read_event.data.ptr = &co_routine->co_event_listener;
    int ret = epoll_ctl(co_routine->co_scheduler->co_epoll, EPOLL_CTL_ADD, co_routine->co_id, &read_event);
    if (ret == -1) goto error_event_callback;
    // make coroutine ready to be executed
    uint64_t count = 1;
    write(co_routine->co_id, &count, sizeof(count));

    return co_routine;


error_event_callback:
    close(co_routine->co_id);
error_co_id:
    return 0;
}

void co_destroy(co_routine_t *co_routine) {
    epoll_ctl(co_routine->co_scheduler->co_epoll, EPOLL_CTL_DEL, co_routine->co_id, 0);
    close(co_routine->co_id);
}

void co_resume(co_routine_t *swap_out, co_routine_t *swap_in) {
    uint64_t count = 1;
    write(swap_in->co_id, &count, sizeof(count));
    swapcontext(&swap_out->co_context, &swap_in->co_scheduler->co_kloopd.co_context);
}

void co_yield(co_routine_t *swap_out) {
    ucontext_t *co_context = swap_out->co_context.uc_link;
    co_routine_t *swap_in = container_of(co_context, co_routine_t, co_context);

    co_resume(swap_out, swap_in);
}


co_scheduler_t *co_scheduler_init(co_scheduler_t *co_scheduler, void (*init)(int, int)) {
    co_scheduler->go = 1;

    co_scheduler->co_epoll = epoll_create(EPOLL_SIZE);
    if (co_scheduler->co_epoll == -1)
        goto error_epoll_create;

    co_scheduler->co_origin.co_scheduler = co_scheduler;
    getcontext(&co_scheduler->co_origin.co_context);

    if (!co_init(&co_scheduler->co_kloopd, &co_scheduler->co_origin, kloopd))
        goto error_kloopd_init;
    heap_t *heap = get_kloopd_heap(&co_scheduler->co_kloopd);
    if (!heap_init(heap))
        goto error_heap_init;

    if (!co_init(&co_scheduler->co_uinit, &co_scheduler->co_kloopd, init))
        goto error_init_init;

    return co_scheduler;


error_init_init:
    heap_destroy(heap);
error_heap_init:
    co_destroy(&co_scheduler->co_kloopd);
error_kloopd_init:
    close(co_scheduler->co_epoll);
error_epoll_create:
    return 0;
}

void co_scheduler_run(co_scheduler_t *co_scheduler) {
    swapcontext(&co_scheduler->co_origin.co_context, &co_scheduler->co_kloopd.co_context);

    co_destroy(&co_scheduler->co_uinit);
    //TODO: mem leak fix
    heap_destroy(get_kloopd_heap(&co_scheduler->co_kloopd));
    co_destroy(&co_scheduler->co_kloopd);
    close(co_scheduler->co_epoll);
}

void co_scheduler_exit(co_scheduler_t *co_scheduler) {
    co_scheduler->go = 0;
}


/** BEGIN: unit test **/
#ifdef __MODULE_COROUTINE__
// gcc -g -Wall -fsanitize=address -D__MODULE_COROUTINE__ coroutine.c utils/heap.c

#include <stdio.h>
#include <stdlib.h>

void sub1(int ptr_high_bits, int ptr_low_bits) {
    printf("[%s] enter\n", __FUNCTION__);
    co_routine_t *co_sub1 = co_get(ptr_high_bits, ptr_low_bits);

    printf("[%s] co_sub1 addr: %#lX\n", __FUNCTION__, (uintptr_t)co_sub1);

    printf("[%s] return\n", __FUNCTION__);
}

void sub2(int ptr_high_bits, int ptr_low_bits) {
    co_routine_t *co_sub2 = co_get(ptr_high_bits, ptr_low_bits);
    printf("[%s] enter\n", __FUNCTION__);

    printf("[%s] co_sub2 addr: %#lX\n", __FUNCTION__, (uintptr_t)co_sub2);

    co_routine_t *co_sub1 = (co_routine_t *)malloc(sizeof(co_routine_t));
    co_init(co_sub1, co_sub2, sub1);
    printf("[%s] co_sub1 addr: %#lX\n", __FUNCTION__, (uintptr_t)co_sub1);
    co_resume(co_sub2, &co_sub2->co_scheduler->co_kloopd);
    co_destroy(co_sub1);
    free(co_sub1);

    printf("[%s] return\n", __FUNCTION__);
}

void init(int ptr_high_bits, int ptr_low_bits) {
    co_routine_t *co_uinit = co_get(ptr_high_bits, ptr_low_bits);
    printf("[%s] enter\n", __FUNCTION__);

    printf("[%s] Hello Coroutine\n", __FUNCTION__);

    co_routine_t *co_sub2 = (co_routine_t *)malloc(sizeof(co_routine_t));
    co_init(co_sub2, co_uinit, sub2);
    printf("[%s] co_sub2 addr: %#lX\n", __FUNCTION__, (uintptr_t)co_sub2);
    co_yield(co_uinit);
    co_destroy(co_sub2);
    free(co_sub2);

    co_scheduler_exit(co_uinit->co_scheduler);
    printf("[%s] return\n", __FUNCTION__);
}

int main(int argc, char *argv[]) {
    co_scheduler_t *co_scheduler = malloc(sizeof(co_scheduler_t));
    co_scheduler_init(co_scheduler, init);
    co_scheduler_run(co_scheduler);
    free(co_scheduler);
    return 0;
}

#endif
/** END: unit test **/