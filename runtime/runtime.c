#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
 * We will rewrite this to be the allocator for our garbage collector
 * in due course.
 */
void* sl_alloc(size_t size)
{
    void* result = calloc(1, size);
    if (result == NULL) {
        perror("out of memory");
        abort();
    }
    return result;
}

void* sl_alloc_des(const char* descriptor)
{
    size_t size = strlen(descriptor);
    void* result = calloc(1, size);
    if (result == NULL) {
        perror("out of memory");
        abort();
    }
    return result;
}
