#include <stdlib.h>
#include "heap.h"


static const size_t HEAP_INIT_CAPACITY = 1024;


heap_t *heap_init(heap_t *heap) {
    heap->heap_size = 0;
    heap->heap_capacity = HEAP_INIT_CAPACITY;
    heap->heap_items = (int64_t **)malloc(sizeof(int64_t *) * HEAP_INIT_CAPACITY);

    if (heap->heap_items)
        return heap;
    else
        return 0;
}

void heap_destroy(heap_t *heap) {
    free(heap->heap_items);
}

size_t heap_size(heap_t *heap) {
    return heap->heap_size;
}

int64_t *heap_top(heap_t *heap) {
    return heap->heap_items[0];
}

int64_t *heap_push(heap_t *heap, int64_t *key) {
    if (heap->heap_size == heap->heap_capacity) {
        heap->heap_capacity *= 2;
        int64_t **enlarge = (int64_t **)realloc(heap->heap_items, heap->heap_capacity);
        if (!enlarge) {
            heap->heap_capacity /= 2;
            return 0;
        }
        heap->heap_items = enlarge;
    }

    heap->heap_items[heap->heap_size] = key;

    int64_t *tmp_key;
    size_t current = heap->heap_size, parent = (current - 1) / 2;
    while (current > 0 && *(heap->heap_items[current]) < *(heap->heap_items[parent])) {
        tmp_key = heap->heap_items[current];
        heap->heap_items[current] = heap->heap_items[parent];
        heap->heap_items[parent] = tmp_key;

        current = parent;
        parent = (current - 1) / 2;
    }

    heap->heap_size++;
    return key;
}

int64_t *heap_pop(heap_t *heap) {
    heap->heap_size--;

    int64_t *pop_key = heap->heap_items[0];

    int64_t *tmp_key;
    heap->heap_items[0] = heap->heap_items[heap->heap_size];
    int current = 0, smallest = current;
    int left = current * 2 + 1, right = current * 2 + 2;
    do {
        if (left < heap->heap_size && *(heap->heap_items[left]) < *(heap->heap_items[smallest]))
            smallest = left;
        if (right < heap->heap_size && *(heap->heap_items[right]) < *(heap->heap_items[smallest]))
            smallest = right;

        if (smallest == current) break;

        // swap
        tmp_key = heap->heap_items[current];
        heap->heap_items[current] = heap->heap_items[smallest];
        heap->heap_items[smallest] = tmp_key;

        current = smallest;
        left = current * 2 + 1, right = current * 2 + 2;
    } while (1);

    return pop_key;
}


/** BEGIN: unit test **/
#ifdef __MODULE_UTILS_HEAP__
// gcc -g -Wall -fsanitize=address -D__MODULE_UTILS_HEAP__ heap.c

#include <stdio.h>

int main(int argc, char *argv[]) {
    {
        heap_t heap;
        heap_init(&heap);
        int64_t keys[] = {8, 6, 4, 2, 0};
        for (int i = 0; i < 5; i++)
            heap_push(&heap, keys + i);
        while (heap_size(&heap))
            printf("%ld ", *heap_pop(&heap));
        printf("\n");
        heap_destroy(&heap);
    }

    {
        heap_t heap;
        heap_init(&heap);
        long keys[] = {1, 3, 5, 7, 9};
        for (int i = 0; i < 5; i++)
            heap_push(&heap, keys + i);
        while (heap_size(&heap))
            printf("%ld ", *heap_pop(&heap));
        printf("\n");
        heap_destroy(&heap);
    }

    {
        heap_t heap;
        heap_init(&heap);
        long keys[] = {1, 8, 4, 7, 5, 0, 6, 2, 3, 9};
        for (int i = 0; i < 10; i++)
            heap_push(&heap, keys + i);
        while (heap_size(&heap))
            printf("%ld ", *heap_pop(&heap));
        printf("\n");
        heap_destroy(&heap);
    }

    return 0;
}

#endif
/** END: unit test **/