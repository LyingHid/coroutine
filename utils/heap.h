#ifndef __HEADER_GLOVE_HEAP__
#define __HEADER_GLOVE_HEAP__


#include <stdlib.h>
#include <stdint.h>


typedef struct __GloveHeapItem {
    int64_t key;
    void *value;
} HeapItem;

typedef struct __GloveHeap {
    size_t heap_size;
    size_t heap_capacity;
    HeapItem *heap_item;
} Heap;


Heap *heap_create(void);
size_t heap_size(Heap *heap);
HeapItem *heap_top(Heap *heap);
int heap_push(Heap *heap, int64_t key, void *value);
void heap_pop(Heap *heap);
void heap_destroy(Heap *heap);


#endif