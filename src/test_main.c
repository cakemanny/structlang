#include "test_harness.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define NELEMS(A) ((sizeof A) / sizeof (A)[0])

static struct test {
    tsh_test_func_t func;
    const char* file;
    const char* name;
} g_tests[128] = {};

static struct test* g_end = g_tests;
static struct test* g_limit = g_tests + NELEMS(g_tests);

static const bool g_active = 1 ;

void
tsh_register_test(
        tsh_test_func_t test_func, const char* file, const char* name)
{
    if (!g_active) {
        return;
    }
    assert(test_func);

    if (g_end >= g_limit) {
        perror("tsh_register_test: limit reached");
        exit(1);
    }

    g_end->func = test_func;
    g_end->file = file;
    g_end->name = name;
    g_end++;
}


int
test_main(int argc, char* argv[])
{

    /*
     * Ideas for improvement
     * - Have the test some way report if it fails, without crashing.
     */

    for (struct test* t = g_tests; t != g_end; t++) {
        t->func();
        fprintf(stdout, "%s: PASS\n", t->name);
    }

    return 0;
}
