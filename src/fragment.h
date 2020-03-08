#ifndef __FRAGMENT_H__
#define __FRAGMENT_H__

#include "activation.h" /* ac_frame_t */
#include "tree.h" /* tree_stm_t */

typedef struct sl_fragment_t sl_fragment_t;

struct sl_fragment_t {
    tree_stm_t* fr_body;
    ac_frame_t* fr_frame;
    sl_fragment_t* fr_list;
};

sl_fragment_t* sl_fragment(tree_stm_t* body, ac_frame_t* frame);

sl_fragment_t* fr_append(sl_fragment_t* hd, sl_fragment_t* to_append);

#endif /* __FRAGMENT_H__ */
