#ifndef __ARENA_H__
#define __ARENA_H__
#include <stddef.h>

#define T Arena_T

typedef struct Arena *T;

/*
 * Allocates a new arena.
 */
T Arena_new();

/*
 * Deallocates and disposes of the arena. ap is set to NULL.
 */
void Arena_dispose(T* ap);


/*
 * Like malloc, but the allocation goes on the arena
 */
void* Arena_alloc(T arena, long nbytes, const char* file, int line)
    __attribute__((malloc))
    __attribute__((alloc_size(2)))
    __attribute__((returns_nonnull));

/*
 * Like calloc, but the allocation goes on the arena
 */
void* Arena_calloc(T arena, long count, long nbytes, const char* file, int line)
    __attribute__((malloc))
    __attribute__((alloc_size(2, 3)))
    __attribute__((returns_nonnull));


/*
 * Like realloc, but allocation goes on the arena
 */
void* Arena_realloc(T arena, void *ptr, ptrdiff_t old_size, ptrdiff_t size, const char* file, int line)
    __attribute__((alloc_size(4)))
    __attribute__((returns_nonnull));


/*
 * May free most recent allocation
 */
void Arena_free(T arena, void* ptr, ptrdiff_t size);

/*
 * Clears out the arena, leaving it empty but ready to use again.
 */
void Arena_clear(T arena);


#undef T
#endif /* __ARENA_H__ */
