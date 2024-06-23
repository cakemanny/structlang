#include <dlfcn.h>
#include <execinfo.h>
#include <stdint.h> // uint32_t, ...
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <assert.h>

#define IsBitSet(x, i) (( (x)[(i)>>6] & (1ULL<<((i)&63)) ) != 0ULL)
#define SetBit(x, i) (x)[(i)>>6] |= (1ULL<<((i)&63))
#define ClearBit(x, i) (x)[(i)>>6] &= (1ULL<<((i)&63)) ^ 0xFFFFFFFFFFFFFFFFULL

#define NELEMS(A) ((sizeof A) / sizeof A[0])

#define fatal(fmt, ...) do { \
    fprintf(stderr, "sl runtime: " fmt "\n", ##__VA_ARGS__); \
    abort(); \
} while (0)
#define pfatal(msg) do { \
    perror("sl runtime: " msg); \
    abort(); \
} while (0)


#if defined(__arm64__)
/* static */
const char* callee_saved[] = {
    "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28",
};

#elif defined(__x86_64__)
/* static */
const char* callee_saved[] = {
    "rbx", "r12", "r13", "r14", "r15",
};
#else
#  error "unsupported target platform"
#endif


typedef struct frame_map_t frame_map_t;
struct frame_map_t {
    /*
     * Link to previous frame map structure. Only used during initial
     * traversal.
     */
    frame_map_t*    prev;
    /*
     * The return address that will appear below the stack frame that
     * is being described here
     */
    void*           ret_addr;
    /*
     * each two bits correspond to a register.
     * 00 = no pointer
     * 01 = pointer here
     * 10 = inherited from parent frame
     *
     * the index of the bit pair corresponds to the index of the callee saved
     * register when put in numerical order (see the callee_saved array)
     */
    uint32_t        cs_bitmap;

    uint16_t        num_arg_words;
    uint16_t        num_frame_words;
    uint16_t        num_spill_words;

    /*
     * An array of 10 4-bit cs register indexes. N used for unused
     * One for each set bit in the spill-inherit bitmap
     */
    uint8_t         spill_reg[5];
    uint8_t         _padding0[1];

    /*
     * The arg bitmap followed directly by the frame bitmap, and then the
     * spill-inherit bitmap.
     */
    uint64_t        bitmaps[0];
};



/*
 * This symbol is emitted by in the structlang compilation unit
 */
extern const frame_map_t* sl_rt_frame_maps;

void print_cs_bitmaps()
{
    const frame_map_t* m = sl_rt_frame_maps;

    int n = NELEMS(callee_saved);
    for (; m ; m = m->prev) {
        fprintf(stderr, "Frame descriptor for: %p\n", m->ret_addr);
        for (int i = 0; i < n; i++) {
            uint32_t cs_bitmap = m->cs_bitmap;
            int value = (cs_bitmap >> (2 * i)) & 0b11;
            fprintf(stderr, "%s: %d \n", callee_saved[i], value);
        }
    }
}


static void
print_stack_backtrace(void* fp)
{
    struct stack_frame {
        struct stack_frame*  prev;
        void*                ret_addr;
    } *frame = fp;


    int n = 128; // cut short after we find main
    for (int i = 0; i < n; i++) {
        Dl_info info;
        if (dladdr(frame->ret_addr, &info) == 0) {
            // unable to find symbol
            fprintf(stderr, "%-3d %-35s 0x%016lx\n", i, "???",
                    (uintptr_t)frame->ret_addr);
        } else {
            char bname[MAXPATHLEN];
            fprintf(stderr, "%-3d %-35s 0x%016lx %s + %ld\n", i,
                    basename_r(info.dli_fname, bname),
                    (uintptr_t)frame->ret_addr, info.dli_sname,
                    frame->ret_addr - info.dli_saddr);
        }

        const frame_map_t* m = sl_rt_frame_maps;
        for (; m ; m = m->prev) {
            if (frame->ret_addr == m->ret_addr) {
                break;
            }
        }
        if (!m) {
            // no more GC'd frames. next ones are the OS or libc
            break;
        }

        // This corresponds to the frame pointed at by frame->prev!
        fprintf(stderr, "\tcs_dispo = ");
        int n = NELEMS(callee_saved);
        for (int i = 0; i < n; i++) {
            uint32_t cs_bitmap = m->cs_bitmap;
            int value = (cs_bitmap >> (2 * i)) & 0b11;
            fprintf(stderr, "%s: %d, ", callee_saved[i], value);
        }
        fprintf(stderr, "\n");

        fprintf(stderr, "\tnum_arg_words = %d\n", m->num_arg_words);
        fprintf(stderr, "\tnum_frame_words = %d\n", m->num_frame_words);
        fprintf(stderr, "\tnum_spill_words = %d\n", m->num_spill_words);

        fprintf(stderr, "\tstack length = %ld\n",
                (ptrdiff_t)frame->prev - (ptrdiff_t)frame);

        fprintf(stderr, "\tspill_reg = ");
        for (int i = 0; i < 5; i++) {
            uint8_t first = m->spill_reg[i] & 0b00001111;
            uint8_t second = (m->spill_reg[i] >> 4) & 0b00001111;
            if (first < NELEMS(callee_saved)){
                fprintf(stderr, "%s ", callee_saved[first]);
            }
            if (second < NELEMS(callee_saved)){
                fprintf(stderr, "%s ", callee_saved[second]);
            }
        }
        fprintf(stderr, "\n");

        frame = frame->prev;
    }
}


/*
 * Each page manages allocations of a particular size.
 */
typedef struct page_desc_t page_desc_t;
struct page_desc_t {
    uint32_t        object_size; // in words

    uint64_t        allocated; // bitmap: 1 means allocated
    uint64_t        mark; // mark bit in Mark Sweep collector
    void*           data; // pointer to 64 objects of object_size length.
    page_desc_t*    next;
};

struct allocator {
    /*
     * index 1 is the list of pages of objects of size 1.
     */
    page_desc_t*    pages[128];

    /* Want to collect statistics on which sizes our program uses */
    long            counter[128];

    page_desc_t*    free_list;

    /* TODO: change this to btree or some other search structure */
    struct superpage {
        page_desc_t*    sp_descs;
        void**          sp_slab;
    }               superpage[16];
} alloc;


static void*
pd_find_slot(page_desc_t* page)
{
    // TODO: replace with bitset intrinsics
    for (int i = 0; i < 64; i++) {
        if ((page->allocated & (1ULL<<i)) == 0ULL) {
            page->allocated |= (1ULL<<i);
            void** base = page->data;
            return base + i;
        }
    }
    return NULL;
}


static struct superpage*
alloc_next_free_sp()
{
    for (int i = 0; i < NELEMS(alloc.superpage); i++) {
        if (alloc.superpage[i].sp_descs == 0) {
            return &(alloc.superpage[i]);
        }
    }
    return NULL;
}

typedef struct {
    void* words[64];
} page_data_t;

static struct superpage*
alloc_new_superpage()
{
    // create new superpage
    struct superpage* sp = alloc_next_free_sp();
    if (!sp) {
        fatal("out of super pages");
    }

    typeof(sp->sp_descs) descs = calloc(64, sizeof *descs);
    if (descs == NULL) {
        pfatal("calloc");
    }

    void* slab = mmap(0, 64 * sizeof(page_data_t),
            PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, 0, 0);
    if (slab == MAP_FAILED) {
        pfatal("mmap");
    }

    // link descriptors together to form the free list
    for (int i = 0; i < 63; i++) {
        descs[i].next = &(descs[i]);
    }
    // point each descriptor to its data page
    for (int i = 0; i < 64; i++) {
        descs[i].data = ((page_data_t*)slab) + i;
    }

    sp->sp_descs = descs;
    sp->sp_slab = slab;
    return sp;
}

static void
alloc_print_stats()
{
    fprintf(stderr, "+-----------+-----------+\n");
    fprintf(stderr, "|      size |     count |\n");
    fprintf(stderr, "+-----------+-----------+\n");
    for (int i = 0; i < NELEMS(alloc.counter); i++) {
        if (alloc.counter[i] > 0) {
            fprintf(stderr, "| %9d | %9ld |\n", i, alloc.counter[i]);
        }
    }
    fprintf(stderr, "+-----------+-----------+\n");
}


/*
 * We will rewrite this to be the allocator for our garbage collector
 * in due course.
 */
void* sl_alloc_des(const char* descriptor)
{
    size_t nwords = strlen(descriptor);

    assert(nwords < 64 && "TODO");
    assert(nwords < (2ULL<<32)); // Keep this one
    alloc.counter[nwords] += 1;

    page_desc_t* page = alloc.pages[nwords];
    if (!page) {
        if (!alloc.free_list) {
            struct superpage* sp = alloc_new_superpage();
            alloc.free_list = sp->sp_descs;
        }
        page = alloc.pages[nwords] = alloc.free_list;
        alloc.free_list = page->next;

        page->object_size = nwords;
    }

    void* slot = pd_find_slot(page);
    if (slot) {
        return slot;
    }

    /*
     * We need to have some sort of idea of heap size
     * , do a CG, then we know heap usage
     * and then if heap usage is above let's say 50% .... maybe
     * we allocate a new super page (or double the number...)
     */


    void* callstack[128];
    int frames = backtrace(callstack, 128);
    backtrace_symbols_fd(callstack, frames, 2);

    /*
     * We should try to implement what backtrace does....
     */

    uintptr_t* fp;
#if defined(__arm64__)
    asm ("mov	%0, fp" : "=r" (fp));
#elif defined(__x86_64__)
    asm ("movq	%%rbp, %0" : "=r" (fp));
#else
#  error "unsupported"
#endif

    fprintf(stderr, "fp = (0x%lx)\n", (uintptr_t)fp);
    fprintf(stderr, "RA = (0x%lx)\n", fp[1]);

    print_stack_backtrace(fp);
    // print_cs_bitmaps();

    alloc_print_stats();
    fatal("TODO: implement garbage collection / add more pages?");
}
