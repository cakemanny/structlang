#ifndef __MEM_H__
#define __MEM_H__
// vim:ft=c:
#include <stdlib.h> // calloc
#include <stdio.h> // perror

#define unlikely(x)    __builtin_expect(!!(x), 0)

static void* xmalloc(size_t size)
    __attribute__((malloc))
    __attribute__((alloc_size(1)))
    __attribute__((returns_nonnull));
static void* xmalloc(size_t size)
{
    void* ptr = calloc(1, size);
    if (unlikely(!ptr)) {
        perror("out of memory");
        abort();
    }
    return ptr;
}

#undef unlikely

#endif
