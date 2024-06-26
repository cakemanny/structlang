#ifndef __LIST_H__
#define __LIST_H__
// vim:ft=c:
#include "interfaces/arena.h"

// basically all of our list types are
struct list_t {
    void* head;
    struct list_t* tail;
};

/*
 * by using void* we avoid otherwise simply having to cast all our
 * various list types
 */
//struct list_t* list_cons(void* head, struct list_t* tail);
void* list_cons(void* head, void* tail, Arena_T);

void* list_reverse(void* list);

int list_length(const void* list);

#endif /* __LIST_H__ */
