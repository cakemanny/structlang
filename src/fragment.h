#ifndef __FRAGMENT_H__
#define __FRAGMENT_H__
// vim:ft=c:

#include "activation.h" /* ac_frame_t */
#include "tree.h" /* tree_stm_t */

typedef struct sl_fragment_t sl_fragment_t;

struct sl_fragment_t {
    enum {
        FR_CODE = 1,
        FR_STRING,
    } fr_tag;
    union {
        struct {
            tree_stm_t* fr_body;
            ac_frame_t* fr_frame;
        }; // FR_CODE
        struct {
            sl_sym_t fr_label;
            const char* fr_string;
        }; // FR_STRING
    };
    sl_fragment_t* fr_list;
};

sl_fragment_t* sl_code_fragment(tree_stm_t* body, ac_frame_t* frame);
sl_fragment_t* sl_string_fragment(sl_sym_t label, const char* string);

sl_fragment_t* fr_append(sl_fragment_t* hd, sl_fragment_t* to_append);

void fr_string_print(FILE* out, const sl_fragment_t* frag);

#endif /* __FRAGMENT_H__ */
