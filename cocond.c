#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include "cocond.h"
#include "cointernal.h"
#include "utils/heap.h"


static void co_cond_eventfd_callback(co_event_listener_t *co_event_listener) {
    co_cond_t *co_cond = container_of(co_event_listener, co_cond_t, co_event_listener);
    int64_t count = 0;

    read(co_cond->cond_id, &count, sizeof(count));
    while (count > 0 && !list_empty(&co_cond->cond_waiters)) {
        list_t *node = list_get_head(&co_cond->cond_waiters);
        co_cond_waiter_t *co_cond_waiter = container_of(node, co_cond_waiter_t, node);
        co_routine_t *co_routine = co_cond_waiter->co_routine;

        if (co_cond_waiter->co_timeout)
            co_cond_waiter->co_timeout->canceled = 1;
        count--;
        list_del(node);
        free(co_cond_waiter);

        swapcontext(&co_cond->co_scheduler->co_kloopd.co_context, &co_routine->co_context);
    }
}

static void co_cond_timeout_callback(co_event_listener_t *co_event_listener) {
    co_timeout_t *co_timeout = container_of(co_event_listener, co_timeout_t, co_event_listener);
    co_cond_timeout_t *co_cond_timeout = container_of(co_timeout, co_cond_timeout_t, co_timeout);

    if (!co_timeout->canceled) {
        co_cond_waiter_t *co_cond_waiter = container_of(co_cond_timeout->node, co_cond_waiter_t, node);
        co_routine_t *co_routine = co_cond_waiter->co_routine;

        list_del(&co_cond_waiter->node);
        free(co_cond_waiter);

        swapcontext(&co_routine->co_scheduler->co_kloopd.co_context, &co_routine->co_context);
    }

    free(co_cond_timeout);
}


co_cond_t *co_cond_init(co_cond_t *co_cond, co_routine_t *co_routine) {
    co_cond->cond_id = eventfd(0, EFD_NONBLOCK);
    if (co_cond->cond_id == -1) goto error_cond_id;
    struct epoll_event read_event;
    read_event.events = EPOLLIN;  // | EPOLLET;
    read_event.data.ptr = &co_cond->co_event_listener;
    int ret = epoll_ctl(co_routine->co_scheduler->co_epoll, EPOLL_CTL_ADD, co_cond->cond_id, &read_event);
    if (ret == -1) goto error_event_callback;

    if (!list_init(&co_cond->cond_waiters)) goto error_list_init;

    co_cond->co_event_listener.callback = co_cond_eventfd_callback;

    co_cond->co_scheduler = co_routine->co_scheduler;

    return co_cond;


error_list_init:
    epoll_ctl(co_routine->co_scheduler->co_epoll, EPOLL_CTL_DEL, co_cond->cond_id, 0);
error_event_callback:
    close(co_cond->cond_id);
error_cond_id:
    return 0;
}

void co_cond_destroy(co_cond_t *co_cond) {
    while (!list_empty(&co_cond->cond_waiters)) {
        list_t *node = list_get_head(&co_cond->cond_waiters);
        co_cond_waiter_t *co_cond_waiter = container_of(node, co_cond_waiter_t, node);
        if (co_cond_waiter->co_timeout)
            co_cond_waiter->co_timeout->canceled = 1;
        list_del(node);
        free(co_cond_waiter);
    }
    list_destroy(&co_cond->cond_waiters);
    epoll_ctl(co_cond->co_scheduler->co_epoll, EPOLL_CTL_DEL, co_cond->cond_id, 0);
    close(co_cond->cond_id);
}

int co_cond_wait(co_cond_t *co_cond, co_routine_t *co_routine, int64_t timeout) {
    if (timeout > 0) {
        // wait with timeout
        co_cond_waiter_t *co_cond_waiter = (co_cond_waiter_t *)malloc(sizeof(co_cond_waiter_t));
        co_cond_timeout_t *co_cond_timeout = (co_cond_timeout_t *)malloc(sizeof(co_cond_timeout_t));
        if (!co_cond_waiter || !co_cond_timeout) {
            if (co_cond_waiter) free(co_cond_waiter);
            if (co_cond_timeout) free(co_cond_timeout);
            return -ENOMEM;
        }

        list_add_tail(&co_cond->cond_waiters, &co_cond_waiter->node);
        co_cond_waiter->co_routine = co_routine;
        co_cond_waiter->co_timeout = &co_cond_timeout->co_timeout;

        co_cond_timeout->co_timeout.canceled = 0;
        co_cond_timeout->co_timeout.heap_key = timeout + get_timestamp();
        co_cond_timeout->co_timeout.co_event_listener.callback = co_cond_timeout_callback;
        co_cond_timeout->node = &co_cond_waiter->node;

        heap_t *heap = get_kloopd_heap(&co_routine->co_scheduler->co_kloopd);
        heap_push(heap, &co_cond_timeout->co_timeout.heap_key);

        co_resume(co_routine, &co_routine->co_scheduler->co_kloopd);

        return EOK;
    } else if (timeout == 0) {
        // try wait
        int64_t count = 0;
        int ret = read(co_cond->cond_id, &count, sizeof(count));

        if (ret == -1) return -errno;

        count = count - 1;
        if (count > 0)
            write(co_cond->cond_id, &count, count);
        return EOK;
    } else {
        // wait until signaled without timeout
        co_cond_waiter_t *co_cond_waiter = (co_cond_waiter_t *)malloc(sizeof(co_cond_waiter_t));
        if (!co_cond_waiter) return -ENOMEM;

        list_add_tail(&co_cond->cond_waiters, &co_cond_waiter->node);
        co_cond_waiter->co_routine = co_routine;
        co_cond_waiter->co_timeout = 0;

        co_resume(co_routine, &co_routine->co_scheduler->co_kloopd);

        return EOK;
    }
}

void co_cond_signal_n(co_cond_t *co_cond, co_routine_t *co_routine, int64_t n) {
    int64_t count = 1;
    write(co_routine->co_id, &count, sizeof(count));
    write(co_cond->cond_id, &n, sizeof(n));

    co_resume(co_routine, &co_routine->co_scheduler->co_kloopd);
}

void co_cond_signal_n_ext(co_cond_t *co_cond, int64_t n) {
    write(co_cond->cond_id, &n, sizeof(n));
}


/** BEGIN: unit test **/
// #ifdef __MODULE_COCOND__
// gcc -g -Wall -fsanitize=address -D__MODULE_COCOND__ cocond.c coroutine.c utils/heap.c utils/list.c

#include <stdio.h>


typedef struct __glove_co_cond_test {
    co_routine_t co_routine;
    co_cond_t    co_cond;
    int          message;
} co_cond_test_t;

void test(int ptr_high_bits, int ptr_low_bits) {
    co_routine_t *co_routine = co_get(ptr_high_bits, ptr_low_bits);
    printf("[%s] enter\n", __FUNCTION__);
    co_cond_test_t *co_test = container_of(co_routine, co_cond_test_t, co_routine);

    co_test->message = 31415926;
    printf("[%s] before signal\n", __FUNCTION__);
    co_cond_signal_n(&co_test->co_cond, co_routine, 1);
    printf("[%s] after signal\n", __FUNCTION__);

    printf("[%s] return\n", __FUNCTION__);
}

void init(int ptr_high_bits, int ptr_low_bits) {
    co_routine_t *co_uinit = co_get(ptr_high_bits, ptr_low_bits);
    printf("[%s] enter\n", __FUNCTION__);

    co_cond_test_t *co_test = (co_cond_test_t *)malloc(sizeof(co_cond_test_t));
    // return to kloopd. make it detached
    co_init(&co_test->co_routine, &co_uinit->co_scheduler->co_kloopd, test);
    co_cond_init(&co_test->co_cond, co_uinit);

    printf("[%s] before wait\n", __FUNCTION__);
    co_cond_wait(&co_test->co_cond, co_uinit, -1);
    printf("[%s] after wait\n", __FUNCTION__);
    printf("[%s] value received %d\n", __FUNCTION__, co_test->message);

    co_cond_destroy(&co_test->co_cond);
    co_destroy(&co_test->co_routine);
    free(co_test);

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

// #endif
/** END: unit test **/