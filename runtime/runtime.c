#include <stdlib.h>
#include <stdio.h>
#include <string.h>


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
    /*
     * The arg bitmap followed directly by the frame bitmap.
     */
    uint64_t        bitmaps[0];
};


/*
 * We will rewrite this to be the allocator for our garbage collector
 * in due course.
 */
void* sl_alloc(size_t size)
{
    void* result = calloc(1, size);
    if (result == NULL) {
        perror("out of memory");
        abort();
    }
    return result;
}

void* sl_alloc_des(const char* descriptor)
{
    size_t size = strlen(descriptor);
    void* result = calloc(1, size);
    if (result == NULL) {
        perror("out of memory");
        abort();
    }
    return result;
}
