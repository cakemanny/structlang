#include "fragment.h"
#include <assert.h> /* assert */
#include "mem.h" // xmalloc

#define var __auto_type

sl_fragment_t* sl_fragment(tree_stm_t* body, ac_frame_t* frame)
{
    assert(body);
    assert(frame);
    sl_fragment_t* x = xmalloc(sizeof *x);
    x->fr_body = body;
    x->fr_frame = frame;
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
