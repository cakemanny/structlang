#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stack_alloc.h"
#include "assertions.h"
#include "mem.h"

stack_alloc_t*
stack_alloc_new()
{
    stack_alloc_t* stack = xmalloc(sizeof *stack);
    return stack;
}

void
stack_dispose(stack_alloc_t** pstack)
{
    while (*pstack) {
        stack_alloc_t* curr = *pstack;
        *pstack = (*pstack)->prev;
        free(curr);
    }
}

void*
stack_alloc(stack_alloc_t* stack, ptrdiff_t nbytes)
{
    assert(nbytes > 0);
    // This would need to becomes 16 byte alignment if buffer is to be
    // passed to syscalls.
    ptrdiff_t align = sizeof(void*);
    ptrdiff_t aligned = nbytes;
    ptrdiff_t padding = -(uintptr_t)stack->offset & (align - 1);
    if (padding != 0) {
        aligned += align - padding;
    }

    enum {page_size = 10 * 1024};
    assert(aligned <= page_size);

    if (stack->offset + aligned > stack->limit) {
        void* new_page = malloc(sizeof *stack + page_size);
        if (!new_page) {
            perror("malloc");
            abort();
        }
        memcpy(new_page, stack, sizeof *stack);
        stack->prev = new_page;
        stack->offset = ((char*)new_page) + sizeof *stack;
        stack->limit = stack->offset + page_size;
    }
    void* p = stack->offset;
    stack->offset += aligned;
    memset(p, 0, aligned);
    return p;
}

void
stack_popto(stack_alloc_t* stack, void* sandline)
{
    char* line = sandline;
    while (!((uintptr_t)line >= (uintptr_t)stack->prev
                && line < stack->offset)) {
        stack_alloc_t* to_free = stack->prev;
        stack->prev = to_free->prev;
        stack->limit = to_free->limit;
        free(to_free);
    }

    assert((uintptr_t)line >= (uintptr_t)(stack->prev + 1));

    stack->offset = line;
}
