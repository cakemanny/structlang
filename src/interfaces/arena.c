#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h> // malloc, abort
#include "arena.h"

#define T Arena_T

// <macros>
#define unlikely(x)    __builtin_expect(!!(x), 0)
#ifdef NDEBUG
#  define THRESHOLD 0
#else
#  define THRESHOLD 10
#endif // NDEBUG

struct Arena {
    T prev;
    char* avail;
    char* limit;
};

union align {
    int i;
    long l;
    long *lp;
    void *p;
    void (*fp)(void);
    float f;
    double d;
    long double ld;
};
#define alignment (sizeof (union align))

union header {
    struct Arena b;
    union align a;
};


static T freechunks;
static int nfree;


T Arena_new()
{
    T arena = malloc(sizeof *arena);
    if (unlikely(!arena)) {
        perror("out of memory");
        abort();
    }
    arena->prev = NULL;
    arena->limit = arena->avail = NULL;
    return arena;
}

void
Arena_dispose(T* ap)
{
    assert(ap && *ap);
    Arena_free(*ap);
    free(*ap);
    *ap = NULL;
}


void*
Arena_alloc(T arena, long nbytes, const char* file, int line)
{
    assert(arena);
    assert(nbytes > 0);
    // <round up to alignment boundary>
    nbytes = ((nbytes + alignment - 1) / alignment) * alignment;
    while (nbytes > arena->limit - arena->avail) {
        // <get new chunk>
        T ptr;
        char *limit;

        if ((ptr = freechunks) != NULL) {
            freechunks = freechunks->prev;
            nfree--;
            limit = ptr->limit;
        } else {
            long m = sizeof (union header) + nbytes + 10*1024;
            ptr = malloc(m);
            if (unlikely(!ptr)) {
                perror("out of memory");
                abort();
            }
            limit = (char*)ptr + m;
        }

        *ptr = *arena;
        arena->avail = (char*)((union header *)ptr + 1);
        arena->limit = limit;
        arena->prev = ptr;
    }
    memset(arena->avail, 0, nbytes);
    arena->avail += nbytes;
    return arena->avail - nbytes;
}

void*
Arena_calloc(T arena, long count, long nbytes, const char* file, int line)
{
    assert(count > 0);
    return Arena_alloc(arena, count*nbytes, file, line);
}


void
Arena_free(T arena)
{
    assert(arena);
    while (arena->prev) {
        struct Arena tmp = *arena->prev;
        if (nfree < THRESHOLD) {
            arena->prev->prev = freechunks;
            freechunks = arena->prev;
            nfree++;
            freechunks->limit = arena->limit;
        } else {
            free(arena->prev);
        }
        *arena = tmp;
    }
    assert(arena->limit == NULL);
    assert(arena->avail == NULL);
}

#undef T

#include "test_harness.h"

void
test_Arena()
{

    Arena_T a = Arena_new();

    assert(a != NULL); // not necessary unless there are bugs


    const char* test_str = "TEST TEST TEST";
    int bufsize = strlen(test_str) + 1;
    char* buf = Arena_alloc(a, bufsize, __FILE__, __LINE__);
    assert(buf != NULL);

    strcpy(buf, test_str);
    assert(strcmp(test_str, buf) == 0);

    Arena_free(a);

    char* buf2 = Arena_alloc(a, bufsize, __FILE__, __LINE__);

    for (int i = 0; i < bufsize; i++) {
        assert(buf[i] == '\0');
    }

    strcpy(buf2, test_str);
    assert(strcmp(test_str, buf2) == 0);

    Arena_dispose(&a);
    assert(a == NULL);

    // This should fail spectacularly. ... but doesn't :(
    assert(strcmp(test_str, buf2) == 0);
}

static void register_tests() __attribute__((constructor));
void
register_tests()  {

    REGISTER_TEST(test_Arena);

}
