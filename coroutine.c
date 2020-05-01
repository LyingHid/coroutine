#include <stddef.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include "coroutine.h"


static void co_routine_eventfd_callback(co_event_listener_t *co_event_listener) {
    co_routine_t *co_routine = container_of(co_event_listener, co_routine_t, co_event_listener);

    // clear event
    uint64_t count;
    read(co_routine->eventfd, &count, sizeof(count));

    swapcontext(&co_routine->co_scheduler->co_kloopd.co_context, &co_routine->co_context);
}


static void kloopd(int ptr_high_bits, int ptr_low_bits) {
    co_routine_t *co_kloopd = co_this(ptr_high_bits, ptr_low_bits);
    co_scheduler_t *co_scheduler = co_kloopd->co_scheduler;

    const size_t EPOLL_EVENT_SIZE = 1024;
    struct epoll_event epoll_events[EPOLL_EVENT_SIZE];
    while (co_scheduler->co_running) {
        int num_events = epoll_wait(co_scheduler->epollfd, epoll_events, EPOLL_EVENT_SIZE, 1000 /* milliseconds */);

        for (int i = 0; i < num_events; i++) {
            co_event_listener_t *co_event_listener = (co_event_listener_t *)epoll_events[i].data.ptr;
            co_event_listener->callback(co_event_listener);
        }
    }
}


co_routine_t *co_init(co_routine_t *co_routine, co_scheduler_t *co_scheduler, void (*fn)(int, int)) {
    co_routine->eventfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (co_routine->eventfd == -1) goto error_co_id;

    getcontext(&co_routine->co_context);
    co_routine->co_context.uc_link = &co_scheduler->co_kloopd.co_context;
    co_routine->co_context.uc_stack.ss_sp = co_routine->co_stack;
    co_routine->co_context.uc_stack.ss_size = CO_STACK_SIZE;
    int ptr_high_bits = (uintptr_t)co_routine >> 32;
    int ptr_low_bits = (uintptr_t)co_routine << 32 >> 32;
    makecontext(&co_routine->co_context, (void (*)(void))fn, 2, ptr_high_bits, ptr_low_bits);

    co_routine->co_event_listener.callback = co_routine_eventfd_callback;

    co_routine->co_scheduler = co_scheduler;

    struct epoll_event read_event;
    read_event.events = EPOLLIN;  // | EPOLLET;
    read_event.data.ptr = &co_routine->co_event_listener;
    int ret = epoll_ctl(co_routine->co_scheduler->epollfd, EPOLL_CTL_ADD, co_routine->eventfd, &read_event);
    if (ret == -1) goto error_event_callback;

    // make coroutine ready to be executed
    uint64_t count = 1;
    write(co_routine->eventfd, &count, sizeof(count));

    return co_routine;


error_event_callback:
    close(co_routine->eventfd);
error_co_id:
    return 0;
}

void co_destroy(co_routine_t *co_routine) {
    epoll_ctl(co_routine->co_scheduler->epollfd, EPOLL_CTL_DEL, co_routine->eventfd, 0);
    close(co_routine->eventfd);
}

void co_resume(co_routine_t *swap_in) {
    uint64_t count = 1;
    write(swap_in->eventfd, &count, sizeof(count));
}

void co_yield(co_routine_t *swap_out) {
    swapcontext(&swap_out->co_context, &swap_out->co_scheduler->co_kloopd.co_context);
}


co_scheduler_t *co_scheduler_init(co_scheduler_t *co_scheduler, void (*uinit)(int, int)) {
    co_scheduler->co_running = 1;

    co_scheduler->epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (co_scheduler->epollfd == -1)
        goto error_epoll_create;

    getcontext(&co_scheduler->ctx_origin);
    getcontext(&co_scheduler->co_kloopd.co_context);
    co_scheduler->co_kloopd.co_context.uc_link = &co_scheduler->ctx_origin;
    co_scheduler->co_kloopd.co_context.uc_stack.ss_sp = co_scheduler->co_kloopd.co_stack;
    co_scheduler->co_kloopd.co_context.uc_stack.ss_size = CO_STACK_SIZE;
    int ptr_high_bits = (uintptr_t)(&co_scheduler->co_kloopd) >> 32;
    int ptr_low_bits = (uintptr_t)(&co_scheduler->co_kloopd) << 32 >> 32;
    makecontext(&co_scheduler->co_kloopd.co_context, (void (*)(void))kloopd, 2, ptr_high_bits, ptr_low_bits);
    co_scheduler->co_kloopd.co_scheduler = co_scheduler;

    if (!co_init(&co_scheduler->co_uinit, co_scheduler, uinit))
        goto error_init_init;

    return co_scheduler;


error_init_init:
    close(co_scheduler->epollfd);
error_epoll_create:
    return 0;
}

void co_scheduler_run(co_scheduler_t *co_scheduler) {
    swapcontext(&co_scheduler->ctx_origin, &co_scheduler->co_kloopd.co_context);

    co_destroy(&co_scheduler->co_uinit);
    close(co_scheduler->epollfd);
}

void co_scheduler_exit(co_scheduler_t *co_scheduler) {
    co_scheduler->co_running = 0;
}


/** BEGIN: unit test **/
#ifdef __MODULE_COROUTINE__
// gcc -g -Wall -fsanitize=address -D__MODULE_COROUTINE__ coroutine.c

#include <stdio.h>
#include <stdlib.h>

void sub2(int ptr_high_bits, int ptr_low_bits) {
    printf("[%s] enter\n", __FUNCTION__);
    co_routine_t *co_sub2 = co_this(ptr_high_bits, ptr_low_bits);

    co_routine_t **env = (co_routine_t **)(co_sub2->co_stack + CO_STACK_SIZE - sizeof(intptr_t));
    co_routine_t *co_sub1 = *env;

    printf("[%s] co_sub1 addr: %#lX\n", __FUNCTION__, (uintptr_t)co_sub1);
    printf("[%s] co_sub2 addr: %#lX\n", __FUNCTION__, (uintptr_t)co_sub2);

    co_resume(co_sub1);
    printf("[%s] return\n", __FUNCTION__);
}

void sub1(int ptr_high_bits, int ptr_low_bits) {
    co_routine_t *co_sub1 = co_this(ptr_high_bits, ptr_low_bits);
    printf("[%s] enter\n", __FUNCTION__);

    printf("[%s] co_sub1 addr: %#lX\n", __FUNCTION__, (uintptr_t)co_sub1);

    co_routine_t *co_sub2 = (co_routine_t *)malloc(sizeof(co_routine_t));
    co_init(co_sub2, co_sub1->co_scheduler, sub2);
    printf("[%s] co_sub2 addr: %#lX\n", __FUNCTION__, (uintptr_t)co_sub2);

    co_routine_t **env = (co_routine_t **)(co_sub2->co_stack + CO_STACK_SIZE - sizeof(intptr_t));
    *env = co_sub1;

    co_yield(co_sub1);

    co_destroy(co_sub2);
    free(co_sub2);

    co_scheduler_exit(co_sub1->co_scheduler);
    printf("[%s] sizeof(co_routine_t) == %lu\n", __FUNCTION__, sizeof(co_routine_t));
    printf("[%s] return\n", __FUNCTION__);
}

int main(int argc, char *argv[]) {
    co_scheduler_t *co_scheduler = malloc(sizeof(co_scheduler_t));
    co_scheduler_init(co_scheduler, sub1);
    co_scheduler_run(co_scheduler);
    free(co_scheduler);
    return 0;
}

#endif
/** END: unit test **/