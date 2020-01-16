#ifndef __HEADER_GLOVE_HEAP__
#define __HEADER_GLOVE_HEAP__


#include <stddef.h>
#include <stdint.h>


typedef struct __glove_heap {
     size_t   heap_size;
     size_t   heap_capacity;
    int64_t **heap_items;
} heap_t;


heap_t *heap_init(heap_t *heap);
void heap_destroy(heap_t *heap);
size_t heap_size(heap_t *heap);
int64_t *heap_top(heap_t *heap);
int64_t *heap_push(heap_t *heap, int64_t *key);
int64_t *heap_pop(heap_t *heap);

#endif