#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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

int asprintf_arena(Arena_T arena, char **ret, const char * fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char one_char[1];
    int len = vsnprintf(one_char, 1, fmt, ap);
    if (len < 1){
        perror("an encoding error occurred");
        abort();
    }
    va_end(ap);

    *ret = Alloc(arena, len + 1);
    va_start(ap, fmt);
    vsnprintf(*ret, len + 1, fmt, ap);
    va_end(ap);
    return len;
}
