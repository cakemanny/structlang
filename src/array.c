#include "array.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void
arrgrow(void *slice, long size)
{
    struct {
        void *data;
        int len;
        int cap;
    } replica;
    memcpy(&replica, slice, sizeof replica);

    replica.cap = replica.cap ? replica.cap : 1;
    replica.cap *= 2;
    void *data = realloc(replica.data, size * replica.cap);
    if (!data) { perror("out of memory"); abort(); }
    //if (replica.len) {
    //    memcpy(data, replica.data, size*replica.len);
    //    free(replica.data);
    //}
    replica.data = data;

    memcpy(slice, &replica, sizeof replica);
}


#include "test_harness.h"

typedef struct {
    int* data;
    int len;
    int cap;
} some_array_t;

void
test_array()
{

    some_array_t a = {};
    assert(a.len == 0);
    assert(a.cap == 0);

    arrpush(&a, 1);
    arrpush(&a, 2);
    arrpush(&a, 3);

    assert(a.len == 3);
    assert(a.cap >= 3);
    for (int i = 0; i < a.len; i++) {
        assert(a.data[i] == i+1);
    }

    arrfree(a);
    assert(a.len == 0);
    assert(a.cap == 0);
    assert(a.data == NULL);
}


static void register_tests() __attribute__((constructor));
void
register_tests() {

    REGISTER_TEST(test_array);

}
