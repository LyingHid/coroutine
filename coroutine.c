#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include "coroutine.h"


#define EPOLL_SIZE 1024

/** from linux/kernel.h
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({				      \
	void *__mptr = (void *)(ptr);					          \
	((type *)((intptr_t)__mptr - __builtin_offsetof(type, member))); })


/** BEGIN: coroutine level api **/

CoRoutine *CoCreate(CoScheduler *co_scheduler, CoRoutine *co_return, void (*fn)(int, int), size_t co_closure) {
    CoRoutine *co_routine = (CoRoutine *)malloc(sizeof(CoRoutine) + co_closure);
    if (!co_routine) return 0;

    co_routine->co_scheduler = co_scheduler;

    co_routine->co_id = eventfd(0, EFD_NONBLOCK);
    if (co_routine->co_id == -1) {
        free(co_routine);
        return 0;
    }

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
    int ret = epoll_ctl(co_scheduler->co_epoll, EPOLL_CTL_ADD, co_routine->co_id, &read_event);
    if (ret == -1) {
        close(co_routine->co_id);
        free(co_routine);
        return 0;
    }

    return co_routine;
}

void CoResume(CoRoutine *swap_out, CoRoutine *swap_in) {
    uint64_t count = 1;
    write(swap_in->co_id, &count, sizeof(count));
    swapcontext(&swap_out->co_context, &swap_in->co_scheduler->co_os.co_context);
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

/** END: coroutine level api **/


/** BEGIN: scheduler level api **/

static void CoOS(int ptr_high_bits, int ptr_low_bits) {
    CoScheduler *co_scheduler = (CoScheduler *)CoGet(ptr_high_bits, ptr_low_bits);

    {
        struct epoll_event read_event;
        read_event.events = EPOLLIN; // | EPOLLET;
        read_event.data.ptr = &co_scheduler->co_os;
        int ret = epoll_ctl(co_scheduler->co_epoll, EPOLL_CTL_ADD, co_scheduler->co_os.co_id, &read_event);
        if (ret == -1) return;
    }

    
    { // launch init coroutine
        uint64_t count = 1;
        write(co_scheduler->co_init->co_id, &count, sizeof(count));
    }

    int go = 1;
    struct epoll_event epoll_events[EPOLL_SIZE];
    while (go) {
        int num_events = epoll_wait(co_scheduler->co_epoll, epoll_events, EPOLL_SIZE, -1);
        for (int i = 0; i < num_events; i++) {
            CoRoutine *co_routine = (CoRoutine *)epoll_events[i].data.ptr;
            
            // clear event
            uint64_t count;
            read(co_routine->co_id, &count, sizeof(count));
            
            if (co_routine->co_id == co_scheduler->co_os.co_id) {
                go = 0;
                break;
            }

            co_routine->epoll_event = epoll_events + i;
            swapcontext(&co_scheduler->co_os.co_context, &co_routine->co_context);
        }
    }
}

CoScheduler *CoInit(void (*co_init)(int, int)) {
    CoScheduler *co_scheduler = (CoScheduler *)malloc(sizeof(CoScheduler));
    if (!co_scheduler) return 0;
    
    co_scheduler->co_epoll = epoll_create(EPOLL_SIZE);
    if (co_scheduler->co_epoll == -1) {
        free(co_scheduler);
        return 0;
    }

    getcontext(&co_scheduler->origin_context);

    co_scheduler->co_init = CoCreate(co_scheduler, &co_scheduler->co_os, co_init, 0);
    if (!co_scheduler->co_init) {
        close(co_scheduler->co_epoll);
        free(co_scheduler);
        return 0;
    }
    
    co_scheduler->co_os.co_id = eventfd(0, EFD_NONBLOCK);
    if (co_scheduler->co_os.co_id == -1) {
        CoDestroy(co_scheduler->co_init);
        close(co_scheduler->co_epoll);
        free(co_scheduler);
    }
    getcontext(&co_scheduler->co_os.co_context);
    co_scheduler->co_os.co_context.uc_link = &co_scheduler->origin_context;
    co_scheduler->co_os.co_context.uc_stack.ss_sp = co_scheduler->co_os.co_stack;
    co_scheduler->co_os.co_context.uc_stack.ss_size = CO_STACK_SIZE;
    int ptr_high_bits = (uintptr_t)co_scheduler >> 32;
    int ptr_low_bits = (uintptr_t)co_scheduler << 32 >> 32;
    makecontext(&co_scheduler->co_os.co_context, (void (*)(void))CoOS, 2, ptr_high_bits, ptr_low_bits);

    return co_scheduler;
}

void CoRun(CoScheduler *co_scheduler) {
    swapcontext(&co_scheduler->origin_context, &co_scheduler->co_os.co_context);

    CoDestroy(co_scheduler->co_init);
    close(co_scheduler->co_os.co_id);
    close(co_scheduler->co_epoll);
    free(co_scheduler);
}

void CoExit(CoScheduler *co_scheduler) {
    uint64_t count = 1;
    write(co_scheduler->co_os.co_id, &count, sizeof(count));
}

/** END: scheduler level api **/


/** BEGIN: unit test **/

#ifdef __MODULE_MAIN__
// gcc -Wall -D__MODULE_MAIN__ coroutine.c #-fsanitize=address

#include <stdio.h>


void sub1(int ptr_high_bits, int ptr_low_bits) {
    printf("[%s] enter\n", __FUNCTION__);
    CoRoutine *co_sub1 = CoGet(ptr_high_bits, ptr_low_bits);
    

    printf("[%s] co_sub1 addr: %#lX\n", __FUNCTION__, (uintptr_t)co_sub1);

    *(int *)co_sub1->co_closure = 13579;
    printf("[%s] yield\n", __FUNCTION__);
    CoYield(co_sub1);
    *(int *)co_sub1->co_closure = 24680;
    
    printf("[%s] return\n", __FUNCTION__);
}

void sub2(int ptr_high_bits, int ptr_low_bits) {
    CoRoutine *co_sub2 = CoGet(ptr_high_bits, ptr_low_bits);
    printf("[%s] enter\n", __FUNCTION__);
    
    printf("[%s] co_sub2 addr: %#lX\n", __FUNCTION__, (uintptr_t)co_sub2);

    CoRoutine *co_sub1 = CoCreate(co_sub2->co_scheduler, co_sub2, sub1, sizeof(int));
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

    CoRoutine *co_sub2 = CoCreate(co_init->co_scheduler, co_init, sub2, 0);
    printf("[%s] co_sub2 addr: %#lX\n", __FUNCTION__, (uintptr_t)co_sub2);
    CoResume(co_init, co_sub2);
    CoDestroy(co_sub2);

    printf("[%s] return\n", __FUNCTION__);
    CoExit(co_init->co_scheduler);
}

int main(int argc, char *argv[]) {
    CoScheduler *co_scheduler = CoInit(init);
    CoRun(co_scheduler);
    return 0;
}

#endif

/** END: unit test **/