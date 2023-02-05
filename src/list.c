#include "list.h"
#include "mem.h"

void* list_cons(void* head, void* tail)
{
    struct list_t* node = xmalloc(sizeof *node);
    node->head = head;
    node->tail = tail;
    return node;
}

void* list_reverse(void* _list)
{
    struct list_t* list = _list;
    struct list_t* head = NULL;
    struct list_t* next = NULL;

    for (; list; list = next) {
        next = list->tail;
        list->tail = head;
        head = list;
    }
    return head;
}

int list_length(const void* _list)
{
    const struct list_t* list = _list;
    int n = 0;
    for (; list; list = list->tail) {
        n++;
    }
    return n;
}
