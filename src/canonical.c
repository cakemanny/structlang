#include "canonical.h"
#include "mem.h"
#include <assert.h> /* assert */
#include <string.h> /* memcpy */
#include "interfaces/table.h"

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
    memcpy(&cl_, cl, sizeof cl_);
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

typedef basic_block_t trace_t;

typedef struct trace_list_t trace_list_t;
struct trace_list_t {
    trace_t* tl_trace;
    trace_list_t* tl_list;
};

static trace_list_t* trace_list_new(trace_t* t)
{
    trace_list_t* tl = xmalloc(sizeof *tl);
    tl->tl_trace = t;
    return tl;
}
static trace_t* append_block_to_trace(trace_t* trace, basic_block_t* b)
{
    assert(b->bb_list == NULL);
    if (!trace)
        return b;
    var end = trace;
    while (end->bb_list)
        end = end->bb_list;
    end->bb_list = b;
    return trace;
}
static trace_list_t* append_trace_to_trace_list(trace_list_t* hd, trace_t* t)
{
    var list_item = trace_list_new(t);
    if (!hd)
        return list_item;
    var end = hd;
    while (end->tl_list)
        end = end->tl_list;
    end->tl_list = list_item;
    return end;
}

static sl_sym_t label_for_block(basic_block_t* b)
{
    assert(b->bb_stmts->tst_tag == TREE_STM_LABEL);
    return b->bb_stmts->tst_label;
}

static tree_stm_t* last_stm_in_block(basic_block_t* b)
{
    var s = b->bb_stmts;
    while (s->tst_list)
        s = s->tst_list;

    assert(s->tst_tag == TREE_STM_JUMP || s->tst_tag == TREE_STM_CJUMP);
    return s;
}

static basic_block_t* remove_block_from_blocks(
        basic_block_t** pQ, basic_block_t* c)
{
    var Q = *pQ;

    if (Q == c) {
        *pQ = Q->bb_list;
        c->bb_list = NULL;
        return c;
    }

    for (var b = Q; b; b = b->bb_list) {
        if (b->bb_list == c) {
            b->bb_list = c->bb_list; c->bb_list = NULL;
            return c;
        }
    }
    assert(0 && "c not in Q");
}

/*
 * XX -> JUMP(L1, [L1]) -> LABEL(L1) -> YY
 * ===>
 * XX -> LABEL(L1) -> YY
 */
static int remove_redundant_unconditional_jumps(tree_stm_t* result)
{
    int ops = 0;
    for (var s0 = result; s0; s0 = s0->tst_list) {
        if (!s0->tst_list)
            continue;

        var s = s0->tst_list;
        if (s->tst_tag == TREE_STM_JUMP && s->tst_jump_num_labels == 1) {
            if (!s->tst_list)
                continue;
            var s2 = s->tst_list;
            if (s2->tst_tag == TREE_STM_LABEL
                    && s2->tst_label == s->tst_jump_labels[0]) {
                s0->tst_list = s2;
                ops++;
            }
        }
    }
    return ops;
}

static tree_relop_t invert_relop(tree_relop_t op)
{
    switch (op) {
        case TREE_RELOP_EQ: return TREE_RELOP_NE;
        case TREE_RELOP_NE: return TREE_RELOP_EQ;

        case TREE_RELOP_LT: return TREE_RELOP_GE;
        case TREE_RELOP_GE: return TREE_RELOP_LT;

        case TREE_RELOP_GT: return TREE_RELOP_LE;
        case TREE_RELOP_LE: return TREE_RELOP_GT;

        case TREE_RELOP_ULT: return TREE_RELOP_UGE;
        case TREE_RELOP_UGE: return TREE_RELOP_ULT;

        case TREE_RELOP_ULE: return TREE_RELOP_UGT;
        case TREE_RELOP_UGT: return TREE_RELOP_ULE;
    }
}

/*
 * CJUMP(<, a, b, Ltrue, Lfalse) -> Ltrue
 * ==>
 * CJUMP(>=, a, b, Lfalse, Ltrue) -> Ltrue
 * or
 * CJUMP(<, a, b, Ltrue, Lfalse) -> Lneither
 * ==>
 * CJUMP(<
 */
static void put_falses_after_cjumps(
        temp_state_t* temp_state, tree_stm_t* result)
{
    for (var s = result; s->tst_list; s = s->tst_list) {
        var s1 = s->tst_list;
        if (s1->tst_tag == TREE_STM_CJUMP) {
            var s2 = s1->tst_list;
            _Bool s2_is_lbl = s2 && s2->tst_tag == TREE_STM_LABEL;
            if (s2_is_lbl && s1->tst_cjump_false == s2->tst_label) {
                // do nothing , we good!
            } else if (s2_is_lbl && s1->tst_cjump_true == s2->tst_label) {
                // invert the operation and flip the labels
                s->tst_list = tree_stm_cjump(
                        invert_relop(s1->tst_cjump_op),
                        s1->tst_cjump_lhs,
                        s1->tst_cjump_rhs,
                        s1->tst_cjump_false,
                        s1->tst_cjump_true);
                s->tst_list->tst_list = s2;
                // leak s1 ,lol
            } else { // neither the t or f label follows
                // add an unconditional jump
                sl_sym_t f0 = temp_newlabel(temp_state);
                s->tst_list = tree_stm_cjump(
                        s1->tst_cjump_op,
                        s1->tst_cjump_lhs,
                        s1->tst_cjump_rhs,
                        s1->tst_cjump_true,
                        f0);
                s->tst_list->tst_list = tree_stm_label(f0);
                s->tst_list->tst_list->tst_list =
                    unconditional_jump(s1->tst_cjump_false);
                s->tst_list->tst_list->tst_list->tst_list = s2;
            }
        }
    }
}

static tree_stm_t* trace_schedule(
        temp_state_t* temp_state, basic_blocks_t blocks)
{
    // if the block is in the table it's not marked
    Table_T table = Table_new(0, NULL, NULL);

    for (basic_block_t* b = blocks.bb_blocks; b; b = b->bb_list) {
        Table_put(table, label_for_block(b), b);
    }

    trace_list_t* traces = NULL;
    var Q = blocks.bb_blocks;
    while (Q) {
        trace_t* T = NULL;
        // remove the head of Q
        var b = remove_block_from_blocks(&Q, Q);
        while (Table_get(table, label_for_block(b))) { // !is_marked(b)
            // mark b
            Table_remove(table, label_for_block(b));

            T = append_block_to_trace(T, b);

            // examine successors of b
            basic_block_t* c = NULL;
            tree_stm_t* last = last_stm_in_block(b);
            if (last->tst_tag == TREE_STM_JUMP) {
                for (int i = 0; i < last->tst_jump_num_labels; i++) {
                    if ((c = Table_get(table, last->tst_jump_labels[i]))) {
                        break;
                    }
                }
            } else if (last->tst_tag == TREE_STM_CJUMP) {
                if ((c = Table_get(table, last->tst_cjump_false))) {
                    // c has been found
                } else if ((c = Table_get(table, last->tst_cjump_true))) {
                    // c has been found
                }
            }
            if (c) {
                // remove c from Q;
                b = remove_block_from_blocks(&Q, c);
            }
        }
        // End trace
        if (T)
            traces = append_trace_to_trace_list(traces, T);
    }

    Table_free(&table);

    // build a new list of basic blocks by following the traces

    // fuck you c
    int num_statements = 0;
    for (var ti = traces; ti; ti = ti->tl_list) {
        var trace = ti->tl_trace;
        for (var bb = trace; bb; bb = bb->bb_list) {
            for (var s = bb->bb_stmts; s; s = s->tst_list){
                num_statements += 1;
            }
        }
    }
    num_statements += 1; // add one for done/end block
    tree_stm_t* stmts_in_order[num_statements];
    int i = 0;
    for (var ti = traces; ti; ti = ti->tl_list) {
        var trace = ti->tl_trace;
        for (var bb = trace; bb; bb = bb->bb_list) {
            for (var s = bb->bb_stmts; s; s = s->tst_list, i++){
                stmts_in_order[i] = s;
            }
        }
    }
    stmts_in_order[num_statements - 1] = tree_stm_label(blocks.bb_end_label);

    for (int i = 0; i < num_statements - 1; i++) {
        stmts_in_order[i]->tst_list = stmts_in_order[i+1];
    }
    stmts_in_order[num_statements - 1]->tst_list = NULL;
    tree_stm_t* result = stmts_in_order[0];

    // reclaim some of the memory here
    for (var ti = traces; ti;) {
        for (var bb = ti->tl_trace; bb; ) {
            ti->tl_trace = bb->bb_list;

            bb->bb_stmts = NULL;
            bb->bb_list = NULL;
            free(bb);

            bb = ti->tl_trace;
        }
        traces = ti->tl_list;

        ti->tl_trace = NULL;
        ti->tl_list = NULL;
        free(ti);

        ti = traces;
    }

    // remove unconditional jumps that are followed by their label
    while (remove_redundant_unconditional_jumps(result) > 0) {
        // ^------ side-effect in loop condition -----^
    }

    // rewrite cjumps so that their false label follows
    put_falses_after_cjumps(temp_state, result);

    return result;
}

/*
 * perform some sanity-checks of our tree
 */
static void verify_statements(tree_stm_t* stmts, const char* check)
{
    // check that labels are present for all jump targets
    int err = 0;
    var t = Table_new(0, NULL, NULL);

    for (var s = stmts; s; s = s->tst_list) {
        if (s->tst_tag == TREE_STM_LABEL) {
            Table_put(t, s->tst_label, s);
        }
    }
    for (var s = stmts; s; s = s->tst_list) {
        if (s->tst_tag == TREE_STM_CJUMP) {
            if (!Table_get(t, s->tst_cjump_true)) {
                fprintf(stderr, "%s: missing %s label\n", check, s->tst_cjump_true);
                err++;
            }
            if (!Table_get(t, s->tst_cjump_false)) {
                fprintf(stderr, "%s: missing %s label\n", check, s->tst_cjump_false);
                err++;
            }
        } else if (s->tst_tag == TREE_STM_JUMP) {
            for (int i = 0 ; i < s->tst_jump_num_labels ; i++) {
                var lbl = s->tst_jump_labels[i];
                if (!Table_get(t, lbl)){
                    fprintf(stderr, "%s: missing %s label\n", check, lbl);
                    err++;
                }
            }
        }
    }

    Table_free(&t);
    assert(err == 0);
}

static void verify_basic_blocks(basic_blocks_t blocks, const char* check)
{
    int err = 0;
    Table_T t = Table_new(0, NULL, NULL);

    for (basic_block_t* b = blocks.bb_blocks; b; b = b->bb_list) {
        for (var s = b->bb_stmts; s; s = s->tst_list) {
            if (s->tst_tag == TREE_STM_LABEL) {
                Table_put(t, s->tst_label, s);
            }
        }
    }
    for (basic_block_t* b = blocks.bb_blocks; b; b = b->bb_list) {
        for (var s = b->bb_stmts; s; s = s->tst_list) {
            if (s->tst_tag == TREE_STM_CJUMP) {
                if (!Table_get(t, s->tst_cjump_true)) {
                    fprintf(stderr, "%s: missing %s label\n", check, s->tst_cjump_true);
                    err++;
                }
                if (!Table_get(t, s->tst_cjump_false)) {
                    fprintf(stderr, "%s: missing %s label\n", check, s->tst_cjump_false);
                    err++;
                }
            } else if (s->tst_tag == TREE_STM_JUMP) {
                for (int i = 0 ; i < s->tst_jump_num_labels ; i++) {
                    var lbl = s->tst_jump_labels[i];
                    if (!Table_get(t, lbl)){
                        fprintf(stderr, "%s: missing %s label\n", check, lbl);
                        err++;
                    }
                }
            }
        }
    }

    Table_free(&t);
    assert(err == 0);
}

sl_fragment_t* canonicalise_tree(
        temp_state_t* temp_state, sl_fragment_t* fragments)
{
    for (var frag = fragments; frag; frag = frag->fr_list) {
        frag->fr_body = linearise(temp_state, frag->fr_body);
        verify_statements(frag->fr_body, "post-linearise");

        basic_blocks_t blocks =
            basic_blocks(temp_state, frag->fr_body);
        verify_basic_blocks(blocks, "post-basic_blocks");

        frag->fr_body = trace_schedule(temp_state, blocks);
        verify_statements(frag->fr_body, "post-trace_schedule");
    }
    return fragments;
}
