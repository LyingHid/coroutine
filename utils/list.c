#include "list.h"


list_t *list_init(list_t *list) {
    list->prev = list;
    list->next = list;
    return list;
}

void list_destroy(list_t *list) {
}

int list_empty(list_t *list) {
    return list->next == list;
}

list_t *list_add_tail(list_t *list, list_t *node) {
    node->next = list;
    node->prev = list->prev;
    list->prev->next = node;
    list->prev = node;
    return node;
}

list_t *list_get_head(list_t *list) {
    return list->next;
}

list_t *list_del(list_t *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    return node;
}


/** BEGIN: unit test **/
#ifdef __MODULE_UTILS_LIST__
// gcc -g -Wall -fsanitize=address -D__MODULE_UTILS_LIST__ list.c

#include <stdio.h>

typedef struct __glove_data {
    list_t node;
    int key;
} data_t;

int main(int argc, char *argv[]) {
    {
        list_t head;
        list_init(&head);
        data_t datas[10];
        for (int i = 0; i < 10; i++) {
            datas[i].key = i;
            list_add_tail(&head, &datas[i].node);
        }
        while (!list_empty(&head)) {
            list_t *node = list_get_head(&head);
            printf("%d ", ((data_t *)node)->key);
            list_del(node);
        }
        printf("\n");
        list_destroy(&head);
    }

    return 0;
}

#endif
/** END: unit test **/