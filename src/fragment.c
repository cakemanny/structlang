#include "fragment.h"
#include <assert.h> /* assert */
#include "format.h"

#define var __auto_type
#define Alloc(arena, size) Arena_alloc(arena, size, __FILE__, __LINE__)

sl_fragment_t*
sl_code_fragment(tree_stm_t* body, ac_frame_t* frame, Arena_T ar)
{
    assert(body);
    assert(frame);
    sl_fragment_t* x = Alloc(ar, sizeof *x);
    x->fr_tag = FR_CODE;
    x->fr_body = body;
    x->fr_frame = frame;
    x->fr_list = NULL;
    return x;
}

sl_fragment_t*
sl_string_fragment(sl_sym_t label, const char* string, Arena_T ar)
{
    assert(label);
    assert(string);
    sl_fragment_t* x = Alloc(ar, sizeof *x);
    x->fr_tag = FR_STRING;
    x->fr_label = label;
    x->fr_string = string;
    x->fr_list = NULL;
    return x;
}

sl_fragment_t*
sl_frame_map_fragment(ac_frame_map_t* map, sl_sym_t ret_label, Arena_T ar)
{
    assert(map);
    assert(ret_label);
    sl_fragment_t* x = Alloc(ar, sizeof *x);
    x->fr_tag = FR_FRAME_MAP;
    x->fr_map = map;
    x->fr_ret_label = ret_label;
    x->fr_list = NULL;
    return x;
}

void sl_fragment_free(sl_fragment_t** pfrag)
{
    var frag = *pfrag;
    switch (frag->fr_tag) {
        case FR_CODE:
            //tree_stm_free(&frag->fr_body); // on frag_arena
            ac_frame_free(&frag->fr_frame);
            break;
        case FR_STRING:
            // free(frag->fr_string); // already on some arena
            break;
        case FR_FRAME_MAP:
            ac_frame_map_free(&frag->fr_map);
            break;
    }
}

void sl_fragment_free_list(sl_fragment_t** pfrag)
{
    while (*pfrag) {
        var to_free = *pfrag;
        *pfrag = to_free->fr_list;
        sl_fragment_free(&to_free);
    }
}

sl_fragment_t* fr_append(sl_fragment_t* hd, sl_fragment_t* to_append)
{
    if (!hd)
        return to_append;

    var final_node = hd;
    while (final_node->fr_list)
        final_node = final_node->fr_list;
    final_node->fr_list = to_append;

    return hd;
}


void fr_string_print(FILE* out, const sl_fragment_t* frag)
{
    assert(frag);
    assert(frag->fr_tag == FR_STRING);

    // TODO bound the size
    size_t required = fmt_escaped_len(frag->fr_string);
    assert(required < 512);
    char buf[512];
    fmt_snprint_escaped(buf, 512, frag->fr_string);
    fprintf(out, "STRING(LABEL(%s), %s)\n",
            frag->fr_label, buf);
}
