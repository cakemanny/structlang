//#define _POSIX_C_SOURCE 1
#include <dlfcn.h> // dladdr, ..
#include <execinfo.h> // backtrace, ..
#include <libgen.h> // basename_r
#include <stdbool.h> // true, false
#include <stdint.h> // uint32_t, ...
#include <stddef.h> // ptrdiff_t
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <assert.h>

#define var __auto_type

#define IsBitSet(x, i) (( (x)[(i)>>6] & (1ULL<<((i)&63)) ) != 0ULL)
#define SetBit(x, i) (x)[(i)>>6] |= (1ULL<<((i)&63))
#define ClearBit(x, i) (x)[(i)>>6] &= (1ULL<<((i)&63)) ^ 0xFFFFFFFFFFFFFFFFULL
#define BitsetLen(len) (((len) + 63) / 64)

#define cs_bitmap_get(x, i) (((x) >> (2 * (i))) & 0b11)
#define CS_BITMAP_SET(x, i, v)  x = (x & (-1 ^ (0b11 << (2*(i))))) | ((v) << (2*(i)))

#define NELEMS(A) ((sizeof A) / sizeof A[0])

#define fatal(fmt, ...) do { \
    fprintf(stderr, "sl runtime: " fmt "\n", ##__VA_ARGS__); \
    abort(); \
} while (0)

#define pfatal(msg) do { \
    perror("sl runtime: " msg); \
    abort(); \
} while (0)

#define debugf(fmt, ...)  do { \
    if (debug) { fprintf(stderr, fmt, ##__VA_ARGS__); } \
} while (0)

// I manually switch this to ifndef when debugging caveman-style
#ifdef XXX
#define odebug(fmt, var_ex) fprintf(stderr, #var_ex " = " fmt "\n", var_ex)
#else
#define odebug(fmt, var_ex) {}
#endif

// Useful note for self when debugging:
// __builtin_debugtrap()
static const bool debug = 0;

#if defined(__arm64__) || defined(__aarch64__)
static const char* callee_saved[] = {
    "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28",
};

#elif defined(__x86_64__)
static const char* callee_saved[] = {
    "rbx", "r12", "r13", "r14", "r15",
};

#else
#  error "unsupported target platform"
#endif

struct cs_context_t {
    void* reg[NELEMS(callee_saved)];
} cs_context;


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


static const frame_map_t*
find_frame_map(void* ret_addr)
{
    // TODO: put the frame maps into a sorted array or search tree.
    const frame_map_t* m = sl_rt_frame_maps;
    for (; m ; m = m->prev) {
        if (ret_addr == m->ret_addr) {
            break;
        }
    }
    return m;
}


static void
print_stack_backtrace(void* fp)
{
    struct stack_frame {
        struct stack_frame*  prev;
        void*                ret_addr;
    } *frame = fp;


    int n = 128; // cut short after we find main
    for (int i = 0; i < n; i++, frame = frame->prev) {
#ifdef __APPLE__
        Dl_info info;
        if (dladdr(frame->ret_addr, &info) == 0) {
            // unable to find symbol
#endif /* __APPLE__ */
            fprintf(stderr, "%-3d %-35s 0x%016lx\n", i, "???",
                    (uintptr_t)frame->ret_addr);
#ifdef __APPLE__
        } else {
            char bname[MAXPATHLEN];
            fprintf(stderr, "%-3d %-35s 0x%016lx %s + %ld\n", i,
                    basename_r(info.dli_fname, bname),
                    (uintptr_t)frame->ret_addr, info.dli_sname,
                    frame->ret_addr - info.dli_saddr);
        }
#endif /* __APPLE__ */

        const frame_map_t* m = find_frame_map(frame->ret_addr);
        if (!m) {
            // no more GC'd frames. next ones are the OS or libc
            break;
        }

        // This corresponds to the frame pointed at by frame->prev!
        fprintf(stderr, "\tcs_dispo = ");
        int n = NELEMS(callee_saved);
        for (int i = 0; i < n; i++) {
            int value = cs_bitmap_get(m->cs_bitmap, i);
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
    }
}


/*
 * Each page manages allocations of a particular size.
 */
typedef struct page_desc_t page_desc_t;
struct page_desc_t {
    uint32_t        object_size; // in words
    /*
     * "Allocated" bits. 1 bit per object. Excess space is unused
     */
    uint64_t        allocated[1];
    /*
     * Mark bits in Mark Sweep collector - 1 bit per object.
     * Remaining space is unused
     */
    uint64_t        mark[1];
    /*
     * 1-bit per word. 1 means there's a pointer.
     */
    uint64_t        ptr_map[1];
    void*           data; // pointer to 64/object_size objects of object_size length.
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

#define SP_NUM_PAGES 64  /* pages per superpage */


static void*
pd_find_slot(page_desc_t* page, const char* descriptor)
{
    static_assert(NELEMS(page->allocated) == 1, "len(page->allocated)");

    for (; page ; page = page->next) {
        int n = 64 / page->object_size;
        // find first zero
        int j = __builtin_ffsll(~page->allocated[0]);
        if (j > 0 && (j - 1) < n) {
            int i = (j - 1);
            SetBit(page->allocated, i);

            int offset = (i * page->object_size);
            for (int j = 0; j < page->object_size; j++) {
                if (descriptor[j] == 'p') {
                    SetBit(page->ptr_map, offset + j);
                } else {
                    ClearBit(page->ptr_map, offset + j);
                }
            }
            void** base = page->data;
            return base + (i * page->object_size);
        }
    }
    return NULL;
}

static bool
pd_is_empty(page_desc_t* page)
{
    static_assert(NELEMS(page->allocated) == 1, "len(page->allocated)");
    return page->allocated[0] == 0ULL;
}

static void
pd_print(page_desc_t* page)
{
    fprintf(stderr, "object_size = %u\n", page->object_size);

    char fmtd[64 * NELEMS(page->allocated) + 1];
    fmtd[64 * NELEMS(page->allocated)] = '\0';
    for (int i =0; i < 64 * NELEMS(page->allocated); i++) {
        fmtd[i] = IsBitSet(page->allocated, i) ? '1' : '0';
    }
    fprintf(stderr, "allocated = 0b%s\n", fmtd);
}


static page_desc_t*
alloc_page_from_free_list(size_t nwords)
{
    var page = alloc.free_list;
    alloc.free_list = page->next;
    page->next = alloc.pages[nwords];
    alloc.pages[nwords] = page;
    page->object_size = nwords;
    return page;
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

    int N = SP_NUM_PAGES;
    typeof(sp->sp_descs) descs = calloc(N, sizeof *descs);
    if (descs == NULL) {
        pfatal("calloc");
    }

    void* slab = mmap(0, N * sizeof(page_data_t),
            PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, 0, 0);
    if (slab == MAP_FAILED) {
        pfatal("mmap");
    }

    // link descriptors together to form the free list
    for (int i = 0; i < N - 1; i++) {
        descs[i].next = &(descs[i + 1]);
    }
    // point each descriptor to its data page
    for (int i = 0; i < N; i++) {
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
 * The allocation function called by the structlang program descriptor
 * is a null terminated C string consisting of 'n's and 'p's for each
 * word to be allocated. 'p' for pointer, 'n' for non-pointer.
 */
void* sl_alloc_des(const char* descriptor);
// we save the callee-saved registers before jumping the to c code
#if defined(__arm64__)
asm ("\
	.global _sl_alloc_des\n\
	.p2align	2\n\
_sl_alloc_des:\n\
	adrp	x8, _cs_context@GOTPAGE\n\
	ldr	x8, [x8, _cs_context@GOTPAGEOFF]\n\
	stp	x19, x20, [x8]\n\
	stp	x21, x22, [x8, #16]\n\
	stp	x23, x24, [x8, #32]\n\
	stp	x25, x26, [x8, #48]\n\
	stp	x27, x28, [x8, #64]\n\
	b	_sl_alloc_des_pt2\n\
");
#elif defined(__aarch64__)
asm ("\
	.global sl_alloc_des\n\
	.p2align	2\n\
sl_alloc_des:\n\
	adrp	x8, cs_context\n\
	add	x8, x8, :lo12:cs_context\n\
	stp	x19, x20, [x8]\n\
	stp	x21, x22, [x8, #16]\n\
	stp	x23, x24, [x8, #32]\n\
	stp	x25, x26, [x8, #48]\n\
	stp	x27, x28, [x8, #64]\n\
	b	sl_alloc_des_pt2\n\
");

// TODO: __x86_64__

#endif


static void find_roots_and_mark(void* fp);
static void sweep_heap();

/*
 * The C part of sl_alloc_des
 */
__attribute__((used))
static void*
sl_alloc_des_pt2(const char* descriptor)
{
    size_t nwords = strlen(descriptor);

    assert(nwords < NELEMS(alloc.counter) && "TODO");
    assert(nwords < (2ULL<<32)); // Keep this one
    alloc.counter[nwords] += 1;
    int retry_count = 0;
retry:
    ;

    page_desc_t* page = alloc.pages[nwords];
    if (!page) {
        if (!alloc.free_list) {
            struct superpage* sp = alloc_new_superpage();
            alloc.free_list = sp->sp_descs;
        }
        page = alloc_page_from_free_list(nwords);
    }

    void* slot = pd_find_slot(page, descriptor);
    if (slot) {
        // Should we zero the memory? Or does the program always initialize
        // the memory?
        return slot;
    }

    // This is falsed out for the sake of the test programs atm
    if (false && alloc.free_list) {
        page = alloc_page_from_free_list(nwords);
        goto retry; // could also just call pd_find_slot, should always work..
    }

    /*
     * We need to have some sort of idea of heap size
     * , do a CG, then we know heap usage
     * and then if heap usage is above let's say 50% .... maybe
     * we allocate a new super page (or double the number...)
     */

    uintptr_t* fp;
#if defined(__arm64__) || defined(__aarch64__)
    asm ("mov	%0, fp" : "=r" (fp));
#elif defined(__x86_64__)
    asm ("movq	%%rbp, %0" : "=r" (fp));
#else
#  error "unsupported"
#endif

    debugf("fp = (0x%lx)\n", (uintptr_t)fp);
    debugf("RA = (0x%lx)\n", fp[1]);

    if (debug) {
        print_stack_backtrace(fp);
    }
    find_roots_and_mark(fp);
    sweep_heap();
    if (retry_count == 0) {
        retry_count += 1;
        goto retry;
    }

    alloc_print_stats();
    debugf("this descriptor '%s'\n", descriptor);
    debugf("this page:\n----\n");
    if (debug){ pd_print(page); };
    debugf("----\n");
    fatal("OUT OF MEMORY");
}


static void
mark_depth_first(void* ptr)
{

    // TODO: just mask off lower bits?
    struct superpage* sp = NULL;
    ptrdiff_t page_num;
    for (int i = 0;
            i < NELEMS(alloc.superpage) && alloc.superpage[i].sp_descs;
            i++) {
        sp = &alloc.superpage[i];
        odebug("%p", sp->sp_slab);

        // >>9 gives the number of multiples of 512 (the page size)
        page_num = ((ptrdiff_t)ptr - (ptrdiff_t)sp->sp_slab) >> 9;
        if (page_num >= 0 && page_num < SP_NUM_PAGES) {
            odebug("%ld", page_num);
            break;
        }
        sp = NULL;
    }
    if (sp == NULL) {
        // Could be a pointer to a stack location?
        fprintf(stderr, "warn: superpage not found for %p\n", ptr);
        return;
    }

    page_desc_t* page = &sp->sp_descs[page_num];

    long word_idx = ((ptrdiff_t)ptr - (ptrdiff_t)page->data) / sizeof(void*);
    odebug("%ld", word_idx);

    int obj_idx = word_idx / page->object_size;
    odebug("%d", obj_idx);

    if (IsBitSet(page->mark, obj_idx)) {
        return;
    }
    SetBit(page->mark, obj_idx);

    // ptr may be an object internal pointer.
    // so we move it back to the object boundary
    word_idx = obj_idx * page->object_size;
    ptr = (void**)page->data + word_idx;

    // use ptr_map to recurse over all the fields
    for (int i = 0, n = page->object_size; i < n; i++) {
        if (IsBitSet(page->ptr_map, word_idx + i)) {
            // TODO: use a stack data structure instead of recursing
            mark_depth_first(((void**)ptr)[i]);
        }
    }
}


struct stack_frame {
    struct stack_frame*  prev;
    void*                ret_addr;
};

static int
deduce_cs_dispo(struct stack_frame *frame, int cs_idx)
{
    assert(cs_idx < NELEMS(callee_saved));

    uint32_t cs_dispo = 0b10;

    const frame_map_t* m;
    for (; (m = find_frame_map(frame->ret_addr)); frame = frame->prev) {
        if (cs_dispo == 2) {
            cs_dispo = cs_bitmap_get(m->cs_bitmap, cs_idx);
        } else {
            break;
        }
    }
    // 0 or 2 both mean not a pointer now we've gone through all the frames.
    return (cs_dispo == 1);
}

static void
find_roots_and_mark(void* fp)
{
    struct stack_frame *frame = fp;

    /*
     * Plan: create a cs_bitmap that we finalize as we go through the
     * frames. i.e. find out whether each callee-saved is a pointer or
     * not.
     * Until we've done that, we check the stack frame itself for roots.
     */
    // start with dispo inherited for all
    uint32_t cs_bitmap = 0b10101010101010101010101010101010;

    for (int i = 0;; i++, frame = frame->prev) {

        const frame_map_t* m = find_frame_map(frame->ret_addr);
        if (!m) {
            // no more GC'd frames. next ones are the OS or libc
            break;
        }

        debugf("%-3d %-35s 0x%016lx\n", i, "???", (uintptr_t)frame->ret_addr);

        for (int i = 0; i < m->num_arg_words; i++) {
            if (IsBitSet(m->bitmaps, i)) {
                debugf("marking arg %d at %p\n", i,
                        (void**)frame->prev + i);
                void* root = *((void**)frame->prev + i);
                mark_depth_first(root);
            }
        }
        var locals_bitmap = m->bitmaps + BitsetLen(m->num_arg_words);
        void** locals_base = (void**)frame->prev - m->num_frame_words;
        for (int i = 0; i < m->num_frame_words; i++) {
            if (IsBitSet(locals_bitmap, i)) {
                debugf("marking slot %d at %p\n", i, locals_base + i);
                void* root = *(locals_base + i);
                mark_depth_first(root);
            }
        }
        var spills_bitmap = locals_bitmap + BitsetLen(m->num_spill_words);
        int j = 0;
        for (int i = 0; i < m->num_spill_words; i++) {
            if (IsBitSet(spills_bitmap, i)) {

                /* see comment for spill_reg */
                int spill_reg_idx = j >> 1;
                uint8_t cs_idx = m->spill_reg[spill_reg_idx];
                if (j & 0b1) {
                    cs_idx >>= 4;
                }
                cs_idx &= 0b00001111;

                assert(cs_idx < NELEMS(callee_saved));
                if (deduce_cs_dispo(frame->prev, cs_idx)) {
                    var cs_name = callee_saved[cs_idx];
                    debugf("marking spilled cs '%s' at slot %d at %p\n",
                            cs_name, i, (void**)locals_base + i);
                    void* root = *((void**)locals_base + i);
                    mark_depth_first(root);
                }
                j++;
            }
        }

        int n = NELEMS(callee_saved);
        for (int i = 0; i < n; i++) {
            if (cs_bitmap_get(cs_bitmap, i) == 2) {
                int value = cs_bitmap_get(m->cs_bitmap, i);
                CS_BITMAP_SET(cs_bitmap, i, value);
            }
        }

    }

    for (int i = 0; i < NELEMS(callee_saved); i++) {
        if (cs_bitmap_get(cs_bitmap, i) == 1) {
            debugf("marking mark cs %s containing addr %p\n",
                    callee_saved[i], cs_context.reg[i]);
            mark_depth_first(cs_context.reg[i]);
        }
    }
}

static void
sweep_page(page_desc_t* page)
{
    assert(page->object_size > 0);
    page_data_t* pd; // only used for counting
    for (int word_idx = 0, obj_idx = 0; word_idx < NELEMS(pd->words);
            word_idx += page->object_size, obj_idx++) {
        if (IsBitSet(page->mark, obj_idx)) {
            ClearBit(page->mark, obj_idx);
        } else {
            ClearBit(page->allocated, obj_idx);
#ifndef NDEBUG
            // ptr_map will get overwritten during allocation anyway.
            // but this might help clean up things for debugging.
            for (int i = 0; i < page->object_size; i++) {
                ClearBit(page->ptr_map, word_idx + i);
            }
#endif
        }
    }
}

static void
sweep_superpage(struct superpage* sp)
{
    for (int i = 0; i < SP_NUM_PAGES; i++) {
        var page = &sp->sp_descs[i];
        if (page->object_size == 0) {
            continue;
        }
        sweep_page(page);
    }
}

static void
sweep_page_chain(page_desc_t** ppage)
{
    while (*ppage) {
        sweep_page(*ppage);

        if (pd_is_empty(*ppage)) {
            var empty_page = *ppage;
            *ppage = (*ppage)->next;

            empty_page->object_size = 0;
            empty_page->next = alloc.free_list;
            alloc.free_list = empty_page;
        } else {
            ppage = &(*ppage)->next;
        }
    }
}

static void
sweep_heap()
{
    for (int i = 0; i < NELEMS(alloc.pages); i++) {
        if ((alloc.pages[i])) {
            sweep_page_chain(&alloc.pages[i]);
        }
    }

    if (false) {
        // This feels more cache-friendly, as it hits the pages in order
        // but it doesn't refill the free_list
        for (int i = 0;
                i < NELEMS(alloc.superpage) && alloc.superpage[i].sp_descs;
                i++) {
            struct superpage* sp = &alloc.superpage[i];
            sweep_superpage(sp);
        }
    }
}
