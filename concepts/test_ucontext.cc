#include <iostream>
#include <stdlib.h>
#include <ucontext.h>


// small stack may cause stack overflow !!!
// using asan to detect the overflow
#define STACK_SIZE 2 * 1024 * 1024


ucontext_t main_context;
ucontext_t sub1_context;
ucontext_t sub2_context;


void say_hello(void) {
    std::cout << "hello coroutine" << std::endl;
}

void switch_main(void) {
    std::cout << "switch to main" << std::endl;
    swapcontext(&sub2_context, &main_context);
}


// g++ -Wall test_ucontext.cc -fsanitize=address
int main(int argc, char *argv[]) {
    getcontext(&main_context);

    getcontext(&sub1_context);
    sub1_context.uc_link = &main_context;
    sub1_context.uc_stack.ss_sp = malloc(STACK_SIZE);
    sub1_context.uc_stack.ss_size = STACK_SIZE;
    makecontext(&sub1_context, say_hello, 0);

    getcontext(&sub2_context);
    sub2_context.uc_link = 0;
    sub2_context.uc_stack.ss_sp = malloc(STACK_SIZE);
    sub2_context.uc_stack.ss_size = STACK_SIZE;
    makecontext(&sub2_context, switch_main, 0);

    swapcontext(&main_context, &sub1_context);
    std::cout << "back to main 1" << std::endl;

    swapcontext(&main_context, &sub2_context);
    std::cout << "back to main 2" << std::endl;

    free(sub1_context.uc_stack.ss_sp);
    free(sub2_context.uc_stack.ss_sp);
    return 0;
}