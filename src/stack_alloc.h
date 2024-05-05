#ifndef __STACK_ALLOC_H__
#define __STACK_ALLOC_H__
#include <stddef.h>

typedef struct stack_alloc {
    struct stack_alloc *prev;
    char* offset;
    char* limit;
} stack_alloc_t;

/*
 * Allocates a new stack allocator. stack_dispose must be called once done
 * with.
 */
stack_alloc_t* stack_alloc_new();
/*
 * Free all memory allocated by the on stack and any book keeping structures.
 */
void stack_dispose(stack_alloc_t**);

void* stack_alloc(stack_alloc_t*, ptrdiff_t nbytes)
    __attribute__((malloc))
    __attribute__((alloc_size(2)))
    __attribute__((returns_nonnull));

/*
 * Sandline is a pointer to a previous stack allocation.
 * All allocations made after and include sandline are freed.
 */
void stack_popto(stack_alloc_t*, void* sandline);


#endif /* __STACK_ALLOC_H__ */
