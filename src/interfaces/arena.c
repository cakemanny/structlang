#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h> // malloc, abort
#include "arena.h"

#if defined(__arm64__) && !defined(NDEBUG)
#define USE_ZONES 1
#include <malloc/malloc.h>
#else
#define USE_ZONES 0
#endif

#define T Arena_T

// <macros>
#define unlikely(x)    __builtin_expect(!!(x), 0)
#define THRESHOLD 10

#define fatal(msg) do { perror(msg); abort(); } while (0)

struct Arena {
#if USE_ZONES
    malloc_zone_t* zone;
#else
    T prev;
    char* avail;
    char* limit;
#endif // USE_ZONES
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


#if ! USE_ZONES
static T freechunks;
static int nfree;
#endif


T Arena_new()
{

    T arena = malloc(sizeof *arena);
    if (unlikely(!arena)) {
        fatal("out of memory");
    }
#if USE_ZONES
    arena->zone = malloc_create_zone(0, 0);
    if (unlikely(!arena->zone)) {
        fatal("malloc_create_zone");
    }
#else
    arena->prev = NULL;
    arena->limit = arena->avail = NULL;
#endif // USE_ZONES
    return arena;
}

void
Arena_dispose(T* ap)
{
    assert(ap && *ap);
#if USE_ZONES
    malloc_destroy_zone((*ap)->zone);
#else
    Arena_free(*ap);
#endif
    free(*ap);
    *ap = NULL;
}


void*
Arena_alloc(T arena, long nbytes, const char* file, int line)
{
    assert(arena);
    assert(nbytes >= 0);
#if USE_ZONES
    void* ptr = malloc_zone_calloc(arena->zone, 1, nbytes);
    if (unlikely(!ptr)) {
        fatal("out of memory");
    }
    return ptr;
#else
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
                fatal("out of memory");
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
#endif // USE_ZONES
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
#if USE_ZONES
    // doesn't exist with zones
    malloc_destroy_zone(arena->zone);
    arena->zone = malloc_create_zone(0, 0);
    if (unlikely(!arena->zone)) {
        fatal("malloc_create_zone");
    }
#else
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
#endif // USE_ZONES
}

#undef T

#include "../test_harness.h"

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
        assert(buf2[i] == '\0');
    }

    strcpy(buf2, test_str);
    assert(strcmp(test_str, buf2) == 0);

    Arena_dispose(&a);
    assert(a == NULL);

    // This should fail spectacularly. ... but doesn't :(
    // except with libgmalloc
    // assert(strcmp(test_str, buf2) == 0);
}

static void register_tests() __attribute__((constructor));
void
register_tests()  {

    REGISTER_TEST(test_Arena);

}
