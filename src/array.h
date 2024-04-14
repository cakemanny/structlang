#ifndef __ARRAY_H__
#define __ARRAY_H__
// vim:ft=c:
#include <stdlib.h> // free

// dynamic array implementation
#define arrmaybegrow(a) ((a)->len >= (a)->cap ? (arrgrow(a, sizeof *(a)->data),0) : 0)
#define arrpush(a, v) (arrmaybegrow(a), ((a)->data)[(a)->len++] = (v))
#define arrfree(a) ((a).data ? ((a).cap = (a).len = 0, free((a).data), (a).data=NULL) : 0)
#define arrlast(a) ((a).data[(a).len - 1])

#define arrtype(vtype) struct { vtype* data; int len; int cap; }

void arrgrow(void *slice, long size);

#endif /* __ARRAY_H__ */
