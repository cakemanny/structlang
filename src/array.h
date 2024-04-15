#ifndef __ARRAY_H__
#define __ARRAY_H__
// vim:ft=c:
#include <stdlib.h> // free
#include "interfaces/arena.h"

// dynamic array implementation
#define arrmaybegrow(a, arena) ((a)->len >= (a)->cap ? (arrgrow(a, sizeof *(a)->data, arena),0) : 0)
#define arrpush(a, arena, v) (arrmaybegrow(a, arena), ((a)->data)[(a)->len++] = (v))
#define arrlast(a) ((a).data[(a).len - 1])

#define arrtype(vtype) struct { vtype* data; int len; int cap; }

void arrgrow(void *slice, long size, Arena_T);

#endif /* __ARRAY_H__ */
