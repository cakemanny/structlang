#include "array.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void
arrgrow(void *slice, long size, Arena_T arena)
{
    arrtype(void) replica;
    memcpy(&replica, slice, sizeof replica);

    // to maybe detect when the slice has not been zeroed
    // (asan should catch this though, I think)
    assert(replica.len <= replica.cap);

    replica.cap = replica.cap ? replica.cap : 1;
    ptrdiff_t nbytes = size * replica.cap;
    void *data = Arena_realloc(arena, replica.data, nbytes, 2 * nbytes,
            __FILE__, __LINE__);
    replica.cap *= 2;
    replica.data = data;

    memcpy(slice, &replica, sizeof replica);
}


#include "test_harness.h"

typedef arrtype(int) some_array_t;

void
test_array()
{
    Arena_T ar = Arena_new();

    some_array_t a = {};
    assert(a.len == 0);
    assert(a.cap == 0);

    arrpush(&a, ar, 1);
    arrpush(&a, ar, 2);
    arrpush(&a, ar, 3);

    assert(a.len == 3);
    assert(a.cap >= 3);
    for (int i = 0; i < a.len; i++) {
        assert(a.data[i] == i+1);
    }
    Arena_dispose(&ar);
}


static void register_tests() __attribute__((constructor));
void
register_tests() {

    REGISTER_TEST(test_array);

}
