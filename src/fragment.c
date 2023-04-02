#include "fragment.h"
#include <assert.h> /* assert */
#include "mem.h" // xmalloc
#include "format.h"

#define var __auto_type

sl_fragment_t* sl_code_fragment(tree_stm_t* body, ac_frame_t* frame)
{
    assert(body);
    assert(frame);
    sl_fragment_t* x = xmalloc(sizeof *x);
    x->fr_tag = FR_CODE;
    x->fr_body = body;
    x->fr_frame = frame;
    x->fr_list = NULL;
    return x;
}

sl_fragment_t* sl_string_fragment(sl_sym_t label, const char* string)
{
    assert(label);
    assert(string);
    sl_fragment_t* x = xmalloc(sizeof *x);
    x->fr_tag = FR_STRING;
    x->fr_label = label;
    x->fr_string = string;
    x->fr_list = NULL;
    return x;
}

sl_fragment_t* sl_frame_map_fragment(ac_frame_map_t* map, sl_sym_t ret_label)
{
    assert(map);
    assert(ret_label);
    sl_fragment_t* x = xmalloc(sizeof *x);
    x->fr_tag = FR_FRAME_MAP;
    x->fr_map = map;
    x->fr_ret_label = ret_label;
    x->fr_list = NULL;
    return x;
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
