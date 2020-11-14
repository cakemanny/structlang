#include "canonical.h"
#include "mem.h"
#include <assert.h> /* assert */
#include <string.h> /* memcpy */

#define var __auto_type

typedef struct canon_stm_exp_pair_t {
    tree_stm_t* cnp_stm;
    tree_exp_t* cnp_exp;
} canon_stm_exp_pair_t;

static canon_stm_exp_pair_t do_exp(temp_state_t*, tree_exp_t*);
static tree_stm_t* do_stm(temp_state_t* temp_state, tree_stm_t* s);

static _Bool is_nop(tree_stm_t* s)
{
    return s->tst_tag == TREE_STM_EXP
        && s->tst_exp->te_tag == TREE_EXP_CONST;
}

static tree_stm_t* seq(tree_stm_t* s1, tree_stm_t* s2)
{
    if (is_nop(s1)) {
        return s2;
    }
    if (is_nop(s2)) {
        return s1;
    }
    return tree_stm_seq(s1, s2);
}

static _Bool commute(tree_stm_t* s, tree_exp_t* e)
{
    // TODO: there is an exercise to find more cases to add here
    return is_nop(s)
        || e->te_tag == TREE_EXP_NAME
        || e->te_tag == TREE_EXP_CONST;
}


static canon_stm_exp_pair_t
    reorder(temp_state_t* temp_state, tree_exp_t* es /*exp list*/)
{
    canon_stm_exp_pair_t result = {};
    if (es == NULL) {
        result.cnp_stm = tree_stm_exp(tree_exp_const(0, 1));
        result.cnp_exp = NULL; // just being explicit
        return result;
    }
    if (es->te_tag == TREE_EXP_CALL) {
        var t = temp_newtemp(temp_state);
        var new_head = tree_exp_eseq(
                tree_stm_move(tree_exp_temp(t, es->te_size), es),
                tree_exp_temp(t, es->te_size)
                );
        new_head->te_list = es->te_list;
        return reorder(temp_state, new_head);
    }

    var rest = es->te_list;
    es->te_list = NULL;

    var stms_and_e = do_exp(temp_state, es);
    var stms = stms_and_e.cnp_stm;
    var e = stms_and_e.cnp_exp;

    var stms2_and_el = reorder(temp_state, rest);
    var stms2 = stms2_and_el.cnp_stm;
    var el = stms2_and_el.cnp_exp;

    if (commute(stms2, e)) {
        result.cnp_stm = seq(stms, stms2);
        e->te_list = el;
        result.cnp_exp = e;
        return result;
    } else {
        var t = temp_newtemp(temp_state);
        result.cnp_stm = seq(seq(
                    stms,
                    tree_stm_move(tree_exp_temp(t, e->te_size),  e)),
                stms2);
        result.cnp_exp = tree_exp_temp(t, e->te_size);
        result.cnp_exp->te_list = el;
        return result;
    }
}


/* exp list -> exp */
typedef struct build_exp_func_t {
    tree_exp_t* (*bep_fn)(tree_exp_t*, void*);
    void* bep_cl;
} build_exp_func_t;

static build_exp_func_t bep_func(
        tree_exp_t* (*fn)(tree_exp_t*, void*),
        void* cl)
{
    build_exp_func_t func = { .bep_fn = fn, .bep_cl = cl};
    return func;
}

static tree_exp_t* bep_call(build_exp_func_t bepf, tree_exp_t* el)
{
    return bepf.bep_fn(el, bepf.bep_cl);
}


static canon_stm_exp_pair_t reorder_exp(
        temp_state_t* temp_state, tree_exp_t* el, build_exp_func_t build)
{
    var stms_and_el2 = reorder(temp_state, el);
    canon_stm_exp_pair_t result = {
        .cnp_stm = stms_and_el2.cnp_stm,
        .cnp_exp = bep_call(build, stms_and_el2.cnp_exp),
    };
    return result;
}


/* exp list -> stm */
typedef struct build_stm_func_t {
    tree_stm_t* (*bsp_fn)(tree_exp_t*, void*);
    void* bsp_cl;
} build_stm_func_t;

static build_stm_func_t bsp_func(
        tree_stm_t* (*fn)(tree_exp_t*, void*),
        void* cl)
{
    build_stm_func_t func = { .bsp_fn = fn, .bsp_cl = cl};
    return func;
}

static tree_stm_t* bsp_call(build_stm_func_t bspf, tree_exp_t* el)
{
    return bspf.bsp_fn(el, bspf.bsp_cl);
}


static tree_stm_t* /* list */ reorder_stm(
        temp_state_t* temp_state, tree_exp_t* el, build_stm_func_t build)
{
    var stms_and_el2 = reorder(temp_state, el);
    var stms = stms_and_el2.cnp_stm;
    var el2 = stms_and_el2.cnp_exp;
    return seq(stms, bsp_call(build, el2));
}


static tree_exp_t* rebuild_binop(tree_exp_t* el, void* cl)
{
    tree_binop_t* pop = cl;
    var rhs = el->te_list;
    var lhs = el;
    // don't leave lhs looking like a list
    lhs->te_list = NULL;
    return tree_exp_binop(*pop, lhs, rhs);
}

static tree_exp_t* rebuild_mem(tree_exp_t* el, void* cl)
{
    size_t* psize = cl;
    return tree_exp_mem(el, *psize);
}

static tree_exp_t* rebuild_call(tree_exp_t* el, void* cl)
{
    var func = el;
    var args = el->te_list;
    // unlist the func
    func->te_list = NULL;
    size_t* psize = cl;
    return tree_exp_call(func, args, *psize);
}

static tree_exp_t* rebuild_exp_other(tree_exp_t* _el, void* cl)
{
    return (tree_exp_t*)cl;
}

static canon_stm_exp_pair_t do_exp(temp_state_t* temp_state, tree_exp_t* e)
{
    switch(e->te_tag) {
        case TREE_EXP_BINOP:
        {
            tree_exp_t* el = tree_exp_append(e->te_lhs, e->te_rhs);
            return reorder_exp(
                    temp_state, el, bep_func(rebuild_binop, &e->te_binop));
        }
        case TREE_EXP_MEM:
        {
            return reorder_exp(
                    temp_state,
                    e->te_mem_addr,
                    bep_func(rebuild_mem, &e->te_size));
        }
        case TREE_EXP_ESEQ:
        {
            var s = e->te_eseq_stm;
            var e2 = e->te_eseq_exp;
            var stms = do_stm(temp_state, s);
            var stms2_and_e3 = do_exp(temp_state, e2);
            var stms2 = stms2_and_e3.cnp_stm;
            var e3 = stms2_and_e3.cnp_exp;
            return (canon_stm_exp_pair_t){
                .cnp_stm = seq(stms, stms2),
                .cnp_exp = e3
            };
        }
        case TREE_EXP_CALL:
        {
            var el = tree_exp_append(e->te_func, e->te_args);
            return reorder_exp(
                    temp_state, el, bep_func(rebuild_call, &e->te_size));
        }
        default:
            return reorder_exp(temp_state, NULL, bep_func(rebuild_exp_other, e));
    }
}

static tree_stm_t* rebuild_jump(tree_exp_t* el, void* cl)
{
    tree_stm_t* old_jump = cl;
    return tree_stm_jump(
            el, old_jump->tst_jump_num_labels, old_jump->tst_jump_labels);
}

static tree_stm_t* rebuild_cjump(tree_exp_t* el, void* cl)
{
    tree_stm_t* old_cjump = cl;
    var rhs = el->te_list;
    var lhs = el;
    // unlist the lhs and rhs
    lhs->te_list = NULL;
    return tree_stm_cjump(
            old_cjump->tst_cjump_op,
            lhs,
            rhs,
            old_cjump->tst_cjump_true,
            old_cjump->tst_cjump_false);
}

static tree_stm_t* rebuild_move_temp_call(tree_exp_t* el, void* cl)
{
    struct {
        tree_exp_t* temp;
        size_t call_size;
    } cl_ = {};
    memcpy(&cl_, cl, sizeof cl);
    var e = el;
    el = el->te_list;
    e->te_list = NULL;
    return tree_stm_move(cl_.temp, tree_exp_call(e, el, cl_.call_size));
}

static tree_stm_t* rebuild_move_temp(tree_exp_t* el, void* cl)
{
    tree_exp_t* temp = cl;
    return tree_stm_move(temp, el);
}

static tree_stm_t* rebuild_move_mem(tree_exp_t* el, void* cl)
{
    size_t* psize = cl;
    var e = el;
    var b = el->te_list;
    e->te_list = NULL;
    return tree_stm_move(tree_exp_mem(e, *psize), b);
}

static tree_stm_t* rebuild_exp_call(tree_exp_t* el, void* cl)
{
    size_t* pcallsize = cl;
    var e = el;
    el = el->te_list;
    e->te_list = NULL;
    return tree_stm_exp(tree_exp_call(e, el, *pcallsize));
}

static tree_stm_t* rebuild_exp(tree_exp_t* el, void* _cl)
{
    return tree_stm_exp(el);
}

static tree_stm_t* rebuild_stm_other(tree_exp_t* _el, void* cl)
{
    return (tree_stm_t*)cl;
}

static tree_stm_t* do_stm(temp_state_t* temp_state, tree_stm_t* s)
{
    switch (s->tst_tag) {
        case TREE_STM_SEQ:
            return seq(
                    do_stm(temp_state, s->tst_seq_s1),
                    do_stm(temp_state, s->tst_seq_s2));
        case TREE_STM_JUMP:
            return reorder_stm(
                    temp_state, s->tst_jump_dst, bsp_func(rebuild_jump, s));
        case TREE_STM_CJUMP:
        {
            tree_exp_t* el = tree_exp_append(
                    s->tst_cjump_lhs, s->tst_cjump_rhs);
            return reorder_stm(
                    temp_state, el, bsp_func(rebuild_cjump, s));
        }
        case TREE_STM_MOVE:
        {
            if (s->tst_move_dst->te_tag == TREE_EXP_TEMP) {
                if (s->tst_move_exp->te_tag == TREE_EXP_CALL) {
                    struct {
                        tree_exp_t* temp;
                        size_t call_size;
                    } cl = {
                        .temp = s->tst_move_dst,
                        .call_size = s->tst_move_exp->te_size
                    };
                    var e = s->tst_move_exp->te_func;
                    var el = s->tst_move_exp->te_args;
                    return reorder_stm(
                            temp_state,
                            tree_exp_append(e, el),
                            bsp_func(rebuild_move_temp_call, &cl));
                }
                return reorder_stm(
                        temp_state,
                        s->tst_move_exp,
                        bsp_func(rebuild_move_temp, s->tst_move_dst));
            }
            if (s->tst_move_dst->te_tag == TREE_EXP_MEM) {
                var e = s->tst_move_dst->te_mem_addr;
                var b = s->tst_move_exp;
                return reorder_stm(
                        temp_state,
                        tree_exp_append(e, b),
                        bsp_func(rebuild_move_mem,
                            &s->tst_move_dst->te_size));
            }
            if (s->tst_move_dst->te_tag == TREE_EXP_ESEQ) {
                var as_seq = tree_stm_seq(
                        s->tst_move_dst->te_eseq_stm,
                        tree_stm_move(
                            s->tst_move_dst->te_eseq_exp, s->tst_move_exp));
                return do_stm(temp_state, as_seq);
            }
            // default case
            return reorder_stm(temp_state, NULL, bsp_func(rebuild_stm_other, s));
        }
        case TREE_STM_EXP:
        {
            if (s->tst_exp->te_tag == TREE_EXP_CALL) {
                var e = s->tst_exp->te_func;
                var el = s->tst_exp->te_args;
                return reorder_stm(
                        temp_state,
                        tree_exp_append(e, el),
                        bsp_func(rebuild_exp_call, &s->tst_exp->te_size));
            }
            var e = s->tst_exp;
            return reorder_stm(temp_state, e, bsp_func(rebuild_exp, NULL));
        }
        case TREE_STM_LABEL:
            return reorder_stm(temp_state, NULL, bsp_func(rebuild_stm_other, s));
    }
}

/**
 * linear gets rid of the top-level SEQs, producing a list
 */
static tree_stm_t* linear(tree_stm_t* head, tree_stm_t* tail)
{
    if (head->tst_tag == TREE_STM_SEQ) {
        return linear(head->tst_seq_s1, linear(head->tst_seq_s2, tail));
    }
    head->tst_list = tail;
    return head;
}

/**
 * From an arbitrary Tree statement, produce a list of cleaned trees
 * satisfying
 *   1. No SEQs or ESEQs
 *   2. The parent of every CALL is an EXP(..) or a MOVE(TEMP t,..)
 */
/* stm -> stm list */
static tree_stm_t* linearise(temp_state_t* temp_state, tree_stm_t* s)
{
    return linear(do_stm(temp_state, s), NULL);
}

typedef struct basic_block_t basic_block_t;
struct basic_block_t {
    tree_stm_t* bb_stmts;
    basic_block_t* bb_list;
};
typedef struct basic_blocks_t {
    basic_block_t* bb_blocks;
    sl_sym_t bb_end_label;
} basic_blocks_t;

static void bb_append_block(
        basic_blocks_t* blocks, basic_block_t* block)
{
    if (blocks->bb_blocks == NULL) {
        blocks->bb_blocks = block;
        return;
    }

    var final_node = blocks->bb_blocks;
    while (final_node->bb_list) {
        final_node = final_node->bb_list;
    }
    final_node->bb_list = block;
}

static tree_stm_t* unconditional_jump(sl_sym_t dst)
{
    sl_sym_t* labels = xmalloc(1 * sizeof *labels);
    labels[0] = dst;
    return tree_stm_jump(tree_exp_name(dst), 1, labels);
}

/**
 * Convert a sequence of tree statements into blocks starting with a label
 * and ending with a jump or cjump
 */
/* stm list -> (stm list list * label) */
static basic_blocks_t basic_blocks(
        temp_state_t* temp_state, tree_stm_t* stmts)
{
    // TODO: if the final statement is already a label, we should remove
    // and use it...
    sl_sym_t done = temp_newlabel(temp_state);

    basic_block_t* curr_block = NULL;
    basic_blocks_t result = { .bb_end_label = done };

    if (stmts->tst_tag != TREE_STM_LABEL) {
        tree_stm_t* start_label =
            tree_stm_label(temp_newlabel(temp_state));
        start_label->tst_list = stmts;
        stmts = start_label;
    }

    tree_stm_t x = {};
    for (var s = stmts; s; s = s->tst_list) {
        if (curr_block == NULL) {
            curr_block = xmalloc(sizeof *curr_block);
        }

        // wierd work around to the fact that our stms can only be in a single
        // list at a time
        x = *s;
        curr_block->bb_stmts = tree_stm_append(curr_block->bb_stmts, s);
        // disconnect the rest of stmts from bb_stmts
        s->tst_list = NULL;
        // allow our loop to continue by setting s to the un-disconnected copy
        s = &x;

        if (s->tst_list == NULL || s->tst_list->tst_tag == TREE_STM_LABEL) {
            if (s->tst_tag != TREE_STM_JUMP && s->tst_tag != TREE_STM_CJUMP) {
                var j = unconditional_jump(
                    s->tst_list
                        ? s->tst_list->tst_label
                        : done
                );
                curr_block->bb_stmts =
                    tree_stm_append(curr_block->bb_stmts, j);
            }
            bb_append_block(&result, curr_block);
            curr_block = NULL;
        } else if (s->tst_tag == TREE_STM_JUMP || s->tst_tag == TREE_STM_CJUMP) {
            // well, if there is no label upcoming, then the following code is
            // dead
            while (s->tst_list && s->tst_list->tst_tag != TREE_STM_LABEL) {
                s = s->tst_list;
            }
        }
    }

    #ifndef NDEBUG
        // check our expected block properties
        for (var b = result.bb_blocks; b; b = b->bb_list) {
            var s = b->bb_stmts;
            // each block starts with a label
            assert(s->tst_tag == TREE_STM_LABEL);
            for (; s->tst_list; s = s->tst_list) {
                // advance to final node
            }
            // each block ends with a jump or cjump
            assert(s->tst_tag == TREE_STM_JUMP || s->tst_tag == TREE_STM_CJUMP);
        }
    #endif

    return result;
}

#ifdef XXX
static tree_stm_t* trace_schedule(basic_blocks_t* blocks)
{

}
#endif

sl_fragment_t* canonicalise_tree(
        temp_state_t* temp_state, sl_fragment_t* fragments)
{
    for (var frag = fragments; frag; frag = frag->fr_list) {
        frag->fr_body = linearise(temp_state, frag->fr_body);

#ifdef XXX
        basic_blocks_t blocks =
#endif
            basic_blocks(temp_state, frag->fr_body);
        // print blocks?
#ifdef XXX
        frag->fr_body = trace_schedule(blocks);
#endif
    }
    // TODO: basic blocks
    // TODO: trace schedule
    return fragments;
}
