#ifndef __TRANSLATE_H__
#define __TRANSLATE_H__
#include "tree.h"
#include "ast.h" /* sl_decl_t */
#include "activation.h" /* ac_frame_t */
#include "fragment.h" /* sl_fragment_t */

struct translate_exp_t;
typedef struct translate_exp_t translate_exp_t;

typedef struct label_bifunc_t {
    tree_stm_t* (*lbf_fn)(sl_sym_t, sl_sym_t, void* cl);
    void* lbf_cl;
} label_bifunc_t;

struct translate_exp_t {
    enum {
        TR_EXP_EX = 1,
        TR_EXP_NX,
        TR_EXP_CX
    } tr_exp_tag;
    union {
        tree_exp_t* tr_exp_ex;
        tree_stm_t* tr_exp_nx;
        label_bifunc_t tr_exp_cx;
    };
};


translate_exp_t* translate_ex(tree_exp_t* e);
translate_exp_t* translate_nx(tree_stm_t* s);
translate_exp_t* translate_cx(label_bifunc_t genstm);

tree_exp_t* translate_un_ex(temp_state_t* ts, translate_exp_t*);
tree_stm_t* translate_un_nx(temp_state_t* ts, translate_exp_t*);
label_bifunc_t translate_un_cx(translate_exp_t*);

/*
 * translate a program in our ast into the tree language IR
 *
 * returns a linked list of fragments, which each contain information
 * frame information and a function body - a statement in the tree IR
 */
sl_fragment_t* translate_program(
        temp_state_t* temp_state, sl_decl_t* program, ac_frame_t* frames);

/* Just a helper function, does not allocate */
label_bifunc_t label_bifunc(
        tree_stm_t* (*fn)(sl_sym_t, sl_sym_t, void* cl),
        void* cl);

#endif /* __TRANSLATE_H__ */
