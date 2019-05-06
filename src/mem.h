#ifndef __MEM_H__
#define __MEM_H__
#include <stdlib.h>

static void* xmalloc(size_t size)
    __attribute__((malloc))
    __attribute__((alloc_size(1)))
    __attribute__((returns_nonnull));
static void* xmalloc(size_t size)
{
    void* ptr = calloc(1, size);
    if (!ptr) {
        perror("out of memory");
        abort();
    }
    return ptr;
}

#endif
