#ifndef __HEADER_GLOVE_LIST__
#define __HEADER_GLOVE_LIST__


#include <stdint.h>


typedef struct __glove_list {
    struct __glove_list *prev;
    struct __glove_list *next;
} list_t;


list_t *list_init(list_t *list);
void list_destroy(list_t *list);
int list_empty(list_t *list);
list_t *list_add_tail(list_t *list, list_t *node);
list_t *list_get_head(list_t *list);
list_t *list_del(list_t *node);


#endif