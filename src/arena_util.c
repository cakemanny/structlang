#include <stddef.h>
#include <string.h>
#include "arena_util.h"

#define Alloc(a, size) Arena_alloc(a, size, __FILE__, __LINE__)

// TODO: arena_copy?

// copy s1 allocating it in arena a
char* strdup_arena(Arena_T a, const char *s1)
{
    size_t len = strlen(s1) + 1;
    char* buf = Alloc(a, len);
    memcpy(buf, s1, len);
    return buf;
}
