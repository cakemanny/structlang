#ifndef _ARENA_UTIL_H__
#define _ARENA_UTIL_H__
#include "interfaces/arena.h"

char * strdup_arena(Arena_T, const char *s1);

int asprintf_arena(Arena_T, char **ret, const char * format, ...)
    __attribute__ ((__format__ (__printf__,3,4)));

#endif /* _ARENA_UTIL_H__ */
