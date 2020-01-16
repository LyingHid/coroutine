#ifndef __HEADER_GLOVE_COINTERNAL__
#define __HEADER_GLOVE_COINTERNAL__


#include <stdint.h>
#include "cotype.h"
#include "utils/heap.h"


/** from linux/kernel.h
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 */
#define container_of(ptr, type, member) ({				      \
	void *__mptr = (void *)(ptr);					          \
	((type *)((intptr_t)__mptr - __builtin_offsetof(type, member))); })


static heap_t *get_kloopd_heap(co_routine_t *co_kloopd) {
    return (heap_t *)co_kloopd->co_scheduler->co_origin.co_stack;
}


#endif