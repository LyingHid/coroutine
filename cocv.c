#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>

#include "cocv.h"


static void co_cv_eventfd_callback(co_event_listener_t *co_event_listener) {
    co_cv_t *co_cv = container_of(co_event_listener, co_cv_t, co_event_listener);

    int64_t count = 0;
    read(co_cv->eventfd, &count, sizeof(count));
    while (count-- > 0 && !list_empty(&co_cv->cv_waiters)) {
        list_t *node = list_get_head(&co_cv->cv_waiters);
        co_cv_waiter_t *co_cv_waiter = container_of(node, co_cv_waiter_t, node);

        co_routine_t *co_routine = co_cv_waiter->co_routine;
        if (co_cv_waiter->co_cv_timeout)
            co_cv_waiter->co_cv_timeout->canceled = 1;

        list_del(node);
        free(co_cv_waiter);

        co_resume(co_routine);
    }
}

static void co_cv_timerfd_callback(co_event_listener_t *co_event_listener) {
    co_cv_timeout_t *co_cv_timeout = container_of(co_event_listener, co_cv_timeout_t, co_event_listener);
    co_cv_waiter_t *co_cv_waiter = container_of(co_cv_timeout->node, co_cv_waiter_t, node);
    co_routine_t *co_routine = co_cv_waiter->co_routine;

    epoll_ctl(co_routine->co_scheduler->epollfd, EPOLL_CTL_DEL, co_cv_timeout->timerfd, 0);
    close(co_cv_timeout->timerfd);

    if (co_cv_timeout->canceled) {
        free(co_cv_timeout);
    } else {
        list_del(&co_cv_waiter->node);
        free(co_cv_waiter);
        free(co_cv_timeout);

        co_resume(co_routine);
    }
}


co_cv_t *co_cv_init(co_cv_t *co_cv, co_scheduler_t *co_scheduler) {
    co_cv->eventfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (co_cv->eventfd == -1) goto error_cv_id;

    struct epoll_event read_event;
    read_event.events = EPOLLIN;  // | EPOLLET;
    read_event.data.ptr = &co_cv->co_event_listener;
    int ret = epoll_ctl(co_scheduler->epollfd, EPOLL_CTL_ADD, co_cv->eventfd, &read_event);
    if (ret == -1) goto error_event_callback;

    if (!list_init(&co_cv->cv_waiters)) goto error_list_init;

    co_cv->co_event_listener.callback = co_cv_eventfd_callback;

    co_cv->co_scheduler = co_scheduler;

    return co_cv;


error_list_init:
    epoll_ctl(co_scheduler->epollfd, EPOLL_CTL_DEL, co_cv->eventfd, 0);
error_event_callback:
    close(co_cv->eventfd);
error_cv_id:
    return 0;
}

void co_cv_destroy(co_cv_t *co_cv) {
    while (!list_empty(&co_cv->cv_waiters)) {
        list_t *node = list_get_head(&co_cv->cv_waiters);
        co_cv_waiter_t *co_cv_waiter = container_of(node, co_cv_waiter_t, node);
        if (co_cv_waiter->co_cv_timeout)
            co_cv_waiter->co_cv_timeout->canceled = 1;
        list_del(node);
        free(co_cv_waiter);
    }
    list_destroy(&co_cv->cv_waiters);
    epoll_ctl(co_cv->co_scheduler->epollfd, EPOLL_CTL_DEL, co_cv->eventfd, 0);
    close(co_cv->eventfd);
}

int co_cv_wait(co_cv_t *co_cv, co_routine_t *co_routine, int64_t wait_ms) {
    if (wait_ms > 0) {
        // wait with timeout

        int ret = 0;

        co_cv_waiter_t *co_cv_waiter = (co_cv_waiter_t *)malloc(sizeof(co_cv_waiter_t));
        co_cv_timeout_t *co_cv_timeout = (co_cv_timeout_t *)malloc(sizeof(co_cv_timeout_t));
        if (!co_cv_waiter || !co_cv_timeout) {
            ret = ENOMEM;
            goto error_malloc;
        }

        co_cv_timeout->timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
        if (co_cv_timeout->timerfd == -1) {
            ret = errno;
            goto error_timerfd_create;
        }

        struct itimerspec spec = {
            .it_interval = {0, 0},  /* non zero for repeated timer */
            .it_value = {
                .tv_sec = wait_ms / 1000000000,
                .tv_nsec = wait_ms % 1000000000
            }
        };
        ret = timerfd_settime(co_cv_timeout->timerfd, 0, &spec, 0);
        if (ret == -1) {
            ret = errno;
            goto error_timerfd_settime;
        }

        struct epoll_event read_event;
        read_event.events = EPOLLIN;  // | EPOLLET;
        read_event.data.ptr = &co_cv_timeout->co_event_listener;
        ret = epoll_ctl(co_cv->co_scheduler->epollfd, EPOLL_CTL_ADD, co_cv_timeout->timerfd, &read_event);
        if (ret == -1) {
            goto error_epoll_ctl;
        }

        co_cv_timeout->canceled = 0;

        co_cv_timeout->co_event_listener.callback = co_cv_timerfd_callback;

        co_cv_timeout->node = &co_cv_waiter->node;

        list_add_tail(&co_cv->cv_waiters, &co_cv_waiter->node);
        co_cv_waiter->co_routine = co_routine;
        co_cv_waiter->co_cv_timeout = co_cv_timeout;

        co_yield(co_routine);

        return co_cv_timeout->canceled ? 0 : ETIMEDOUT;


    error_epoll_ctl:
    error_timerfd_settime:
        close(co_cv_timeout->timerfd);
    error_timerfd_create:
    error_malloc:
        if (co_cv_waiter) free(co_cv_waiter);
        if (co_cv_timeout) free(co_cv_timeout);
        return ret;
    } else if (wait_ms == 0) {
        // try wait
        int64_t count = 0;
        int ret = read(co_cv->eventfd, &count, sizeof(count));

        if (ret == -1) return errno;

        if (count-- > 1)
            write(co_cv->eventfd, &count, count);
        return 0;
    } else {
        // wait until signaled without timeout
        co_cv_waiter_t *co_cv_waiter = (co_cv_waiter_t *)malloc(sizeof(co_cv_waiter_t));
        if (!co_cv_waiter) return -ENOMEM;

        list_add_tail(&co_cv->cv_waiters, &co_cv_waiter->node);
        co_cv_waiter->co_routine = co_routine;
        co_cv_waiter->co_cv_timeout = 0;

        co_yield(co_routine);

        return 0;
    }
}

void co_cv_signal(co_cv_t *co_cv, int64_t n) {
    write(co_cv->eventfd, &n, sizeof(n));
}


/** BEGIN: unit test **/
// #ifdef __MODULE_COCOND__
// gcc -g -Wall -fsanitize=address -D__MODULE_COCOND__ cocv.c coroutine.c utils/list.c

#include <stdio.h>
#include <stdlib.h>

void init(int ptr_high_bits, int ptr_low_bits) {
    co_routine_t *co_uinit = co_this(ptr_high_bits, ptr_low_bits);
    printf("[%s] enter\n", __FUNCTION__);

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