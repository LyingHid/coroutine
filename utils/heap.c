#include "heap.h"


Heap *heap_create(void) {
    Heap *heap = (Heap *)malloc(sizeof(Heap));
    if (!heap) goto error;

    heap->heap_item = (HeapItem *)malloc(sizeof(HeapItem) * 1024);
    if (!heap->heap_item) goto error_malloc;

    heap->heap_size = 0;
    heap->heap_capacity = 1024;
    return heap;


error_malloc:
    free(heap);
error:
    return 0;
}

size_t heap_size(Heap *heap) {
    return heap->heap_size;
}

HeapItem *heap_top(Heap *heap) {
    return heap->heap_item;
}

int heap_push(Heap *heap, int64_t key, void *value) {
    if (heap->heap_size == heap->heap_capacity) {
        heap->heap_capacity *= 2;
        HeapItem *enlarge = (HeapItem *)realloc(heap->heap_item, heap->heap_capacity);
        if (!enlarge) {
            heap->heap_capacity /= 2;
            return -1;
        }
        heap->heap_item = enlarge;
    }

    heap->heap_item[heap->heap_size].key = key;
    heap->heap_item[heap->heap_size].value = value;

    HeapItem temp_item;
    int current = heap->heap_size, parent = (current - 1) / 2;
    while (current > 0 && heap->heap_item[current].key < heap->heap_item[parent].key) {
        temp_item = heap->heap_item[current];
        heap->heap_item[current] = heap->heap_item[parent];
        heap->heap_item[parent] = temp_item;

        current = parent;
        parent = (current - 1) / 2;
    }

    heap->heap_size++;
    return 0;
}

void heap_pop(Heap *heap) {
    heap->heap_size--;
    if (!heap->heap_size) return;

    heap->heap_item[0] = heap->heap_item[heap->heap_size];
    
    HeapItem temp_item;
    int current = 0, smallest = current;
    int left = current * 2 + 1, right = current * 2 + 2;
    do {
        if (left < heap->heap_size && heap->heap_item[left].key < heap->heap_item[smallest].key)
            smallest = left;
        if (right < heap->heap_size && heap->heap_item[right].key < heap->heap_item[smallest].key)
            smallest = right;
        
        if (smallest == current) break;

        // swap
        temp_item = heap->heap_item[current];
        heap->heap_item[current] = heap->heap_item[smallest];
        heap->heap_item[smallest] = temp_item;
        
        current = smallest;
        left = current * 2 + 1, right = current * 2 + 2;
    } while (1);
}


void heap_destroy(Heap *heap) {
    free(heap->heap_item);
    free(heap);
}


/** BEGIN: unit test **/

#ifdef __MODULE_UTILS_HEAP__
// gcc -Wall -D__MODULE_UTILS_HEAP__ heap.c #-fsanitize=address

#include <stdio.h>


int main(int argc, char *argv[]) {
    {
        Heap *heap = heap_init();
        for (int i = 8; i >= 0; i -= 2)
            heap_push(heap, i, 0);
        while (heap_size(heap)) {
            printf("%ld ", heap_top(heap)->key);
            heap_pop(heap);
        }
        printf("\n");
        free(heap);
    }

    {
        Heap *heap = heap_init();
        for (int i = 1; i <= 9; i += 2)
            heap_push(heap, i, 0);
        while (heap_size(heap)) {
            printf("%ld ", heap_top(heap)->key);
            heap_pop(heap);
        }
        printf("\n");
        free(heap);
    }

    {
        int keys[] = {1, 8, 4, 7, 5, 0, 6, 2, 3, 9};
        Heap *heap = heap_init();
        for (int i = 0; i < 10; i++)
            heap_push(heap, keys[i], 0);
        while (heap_size(heap)) {
            printf("%ld ", heap_top(heap)->key);
            heap_pop(heap);
        }
        printf("\n");
        free(heap);
    }

    return 0;
}

#endif

/** END: unit test **/