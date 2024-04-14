#include "canonical.h"
#include <assert.h> /* assert */
#include <stdbool.h> /* bool */
#include "interfaces/arena.h"
#include "interfaces/table.h"
#include "array.h"

#define var __auto_type
#define Alloc(arena, size) Arena_alloc(arena, size, __FILE__, __LINE__)

static const bool debug = 0;

typedef struct canon_info_t {
    temp_state_t* temp_state;
    const target_t* target;
    Arena_T arena;
    Arena_T scratch;
} canon_info_t;

typedef struct canon_stm_exp_pair_t {
    tree_stm_t* cnp_stm;
    tree_exp_t* cnp_exp;
} canon_stm_exp_pair_t;

static canon_stm_exp_pair_t do_exp(canon_info_t*, tree_exp_t*);
static tree_stm_t* do_stm(canon_info_t*, tree_stm_t* s);

static bool is_nop(tree_stm_t* s)
{
    return s->tst_tag == TREE_STM_EXP
        && s->tst_exp->te_tag == TREE_EXP_CONST;
}

static bool is_const(const target_t* target, tree_exp_t* e)
{
    return e->te_tag == TREE_EXP_CONST
        // The frame pointer is constant for the duration of the body of the
        // function
        || (e->te_tag == TREE_EXP_TEMP && e->te_temp.temp_id == target->tgt_fp.temp_id)
        || (e->te_tag == TREE_EXP_BINOP && is_const(target, e->te_lhs) && is_const(target, e->te_rhs));
}

static tree_stm_t* seq(tree_stm_t* s1, tree_stm_t* s2, Arena_T ar)
{
    if (is_nop(s1)) {
        return s2;
    }
    if (is_nop(s2)) {
        return s1;
    }
    return tree_stm_seq(s1, s2, ar);
}


static bool may_define_temps(canon_info_t* info, tree_stm_t* s, tree_exp_t* e)
{
    switch (e->te_tag) {
        case TREE_EXP_CONST:
            return false;
        case TREE_EXP_NAME:
            return false;
        case TREE_EXP_TEMP:
            // non-fp machine registers are easily clobbered by calls.
            if (temp_is_machine(e->te_temp)
                    && e->te_temp.temp_id != info->target->tgt_fp.temp_id) {
                return true;
            }
            switch (s->tst_tag) {
                case TREE_STM_MOVE:
                    if (s->tst_move_dst->te_tag == TREE_EXP_TEMP &&
                            s->tst_move_dst->te_temp.temp_id == e->te_temp.temp_id) {
                        return true;
                    }
                    // This is the key result. This move does not define this
                    // temp.
                    return false;
                case TREE_STM_EXP:
                    // This will be a call or something that has no effect
                    // calls themselves do not define non-machine temps
                    return false;
                case TREE_STM_JUMP:
                    return true;
                case TREE_STM_CJUMP:
                    return true;
                case TREE_STM_SEQ:
                    return may_define_temps(info, s->tst_seq_s1, e)
                        || may_define_temps(info, s->tst_seq_s2, e);
                case TREE_STM_LABEL:
                    return true;
            }
            break;
        case TREE_EXP_BINOP:
            return may_define_temps(info, s, e->te_lhs)
                || may_define_temps(info, s, e->te_rhs);
        case TREE_EXP_MEM:
            // not checking for now
            return true;
        case TREE_EXP_CALL:
            return true;
        case TREE_EXP_ESEQ:
            assert(false);
            break;
    }
}

/*
 * Checks whether the statement(s) s and the expression e commute
 *
 * e has already had all ESEQs removed, and calls are not nested within
 *
 */
static bool commute(canon_info_t* info, tree_stm_t* s, tree_exp_t* e)
{
    bool result = is_nop(s)
        || e->te_tag == TREE_EXP_NAME
        || is_const(info->target, e);
    if (result) {
        return result;
    }
    return !may_define_temps(info, s, e);

    // The condition is that any temporaries or memory locations
    // assigned by s, none are referenced in e

    // if s and e contain no calls, and the lhs of any statement in s is a move
    //   MOVE(t0, ...) and e commute if t0 does not appear in e
    //   MOVE(MEM(e0), ...) and e1 commute if e0 is constant (except referring
    //   to the frame pointer) and the location is not read by e

    // first thing to do would be to print non-commuters, to see what is worthy
    // of tackling
}


static canon_stm_exp_pair_t
    reorder(canon_info_t* info, tree_exp_t* es /*exp list*/)
{
    canon_stm_exp_pair_t result = {};
    Arena_T ar = info->arena;
    if (es == NULL) {
        // TODO: should be size zero
        result.cnp_stm = tree_stm_exp(
                tree_exp_const(0, ac_word_size, tree_typ_void(ar), ar), ar);
        result.cnp_exp = NULL; // just being explicit
        return result;
    }
    if (es->te_tag == TREE_EXP_CALL) {
        var t = temp_newtemp(info->temp_state, es->te_size, tree_dispo_from_type(es->te_type));
        var new_head = tree_exp_eseq(
                tree_stm_move(tree_exp_temp(t, es->te_size, es->te_type, ar), es, ar),
                tree_exp_temp(t, es->te_size, es->te_type, ar),
                ar
                );
        new_head->te_list = es->te_list;
        return reorder(info, new_head);
    }

    var rest = es->te_list;
    es->te_list = NULL;

    var stms_and_e = do_exp(info, es);
    var stms = stms_and_e.cnp_stm;
    var e = stms_and_e.cnp_exp;

    var stms2_and_el = reorder(info, rest);
    var stms2 = stms2_and_el.cnp_stm;
    var el = stms2_and_el.cnp_exp;

    if (commute(info, stms2, e)) {
        result.cnp_stm = seq(stms, stms2, ar);
        e->te_list = el;
        result.cnp_exp = e;
        return result;
    } else {
        if (debug) {
            tree_printf(stderr, "do not commute: %S <-> %E\n", stms2, e);
        }

        var t = temp_newtemp(info->temp_state, e->te_size, tree_dispo_from_type(e->te_type));
        result.cnp_stm = seq(seq(
                    stms,
                    tree_stm_move(tree_exp_temp(t, e->te_size, e->te_type, ar),  e, ar), ar),
                stms2, ar);
        result.cnp_exp = tree_exp_temp(t, e->te_size, e->te_type, ar);
        result.cnp_exp->te_list = el;
        return result;
    }
}


/* exp list -> exp */
typedef struct build_exp_func_t {
    tree_exp_t* (*bep_fn)(tree_exp_t*, void*, Arena_T);
    void* bep_cl;
} build_exp_func_t;

static build_exp_func_t bep_func(
        tree_exp_t* (*fn)(tree_exp_t*, void*, Arena_T),
        void* cl)
{
    build_exp_func_t func = { .bep_fn = fn, .bep_cl = cl};
    return func;
}

static tree_exp_t* bep_call(build_exp_func_t bepf, tree_exp_t* el, Arena_T a)
{
    return bepf.bep_fn(el, bepf.bep_cl, a);
}


static canon_stm_exp_pair_t reorder_exp(
        canon_info_t* info, tree_exp_t* el, build_exp_func_t build)
{
    var stms_and_el2 = reorder(info, el);
    canon_stm_exp_pair_t result = {
        .cnp_stm = stms_and_el2.cnp_stm,
        .cnp_exp = bep_call(build, stms_and_el2.cnp_exp, info->arena),
    };
    return result;
}


/* exp list -> stm */
typedef struct build_stm_func_t {
    tree_stm_t* (*bsp_fn)(tree_exp_t*, void*, Arena_T);
    void* bsp_cl;
} build_stm_func_t;

static build_stm_func_t bsp_func(
        tree_stm_t* (*fn)(tree_exp_t*, void*, Arena_T),
        void* cl)
{
    build_stm_func_t func = { .bsp_fn = fn, .bsp_cl = cl};
    return func;
}

static tree_stm_t* bsp_call(build_stm_func_t bspf, tree_exp_t* el, Arena_T a)
{
    return bspf.bsp_fn(el, bspf.bsp_cl, a);
}


static tree_stm_t* /* list */ reorder_stm(
        canon_info_t* info, tree_exp_t* el, build_stm_func_t build)
{
    var stms_and_el2 = reorder(info, el);
    var stms = stms_and_el2.cnp_stm;
    var el2 = stms_and_el2.cnp_exp;
    return seq(stms, bsp_call(build, el2, info->arena), info->arena);
}


static tree_exp_t* rebuild_binop(tree_exp_t* el, void* cl, Arena_T a)
{
    tree_binop_t* pop = cl;
    var rhs = el->te_list;
    var lhs = el;
    // don't leave lhs looking like a list
    lhs->te_list = NULL;
    return tree_exp_binop(*pop, lhs, rhs, a);
}

static tree_exp_t* rebuild_mem(tree_exp_t* el, void* cl, Arena_T a)
{
    tree_exp_t* orig_e = cl;
    return tree_exp_mem(el, orig_e->te_size, orig_e->te_type, a);
}

static tree_exp_t* rebuild_call(tree_exp_t* el, void* cl, Arena_T a)
{
    var func = el;
    var args = el->te_list;
    // unlist the func
    func->te_list = NULL;
    tree_exp_t* e = cl;
    return tree_exp_call(func, args, e->te_size, e->te_type, e->te_ptr_map, a);
}

static tree_exp_t* rebuild_exp_other(tree_exp_t* _el, void* cl, Arena_T _a)
{
    return (tree_exp_t*)cl;
}

static canon_stm_exp_pair_t do_exp(canon_info_t* info, tree_exp_t* e)
{
    switch(e->te_tag) {
        case TREE_EXP_BINOP:
        {
            tree_exp_t* el = tree_exp_append(e->te_lhs, e->te_rhs);
            return reorder_exp(
                    info, el, bep_func(rebuild_binop, &e->te_binop));
        }
        case TREE_EXP_MEM:
        {
            return reorder_exp(
                    info, e->te_mem_addr, bep_func(rebuild_mem, e));
        }
        case TREE_EXP_ESEQ:
        {
            var s = e->te_eseq_stm;
            var e2 = e->te_eseq_exp;
            var stms = do_stm(info, s);
            var stms2_and_e3 = do_exp(info, e2);
            var stms2 = stms2_and_e3.cnp_stm;
            var e3 = stms2_and_e3.cnp_exp;
            return (canon_stm_exp_pair_t){
                .cnp_stm = seq(stms, stms2, info->arena),
                .cnp_exp = e3
            };
        }
        case TREE_EXP_CALL:
        {
            var el = tree_exp_append(e->te_func, e->te_args);
            return reorder_exp(info, el, bep_func(rebuild_call, e));
        }
        default:
            return reorder_exp(info, NULL, bep_func(rebuild_exp_other, e));
    }
}

static tree_stm_t* rebuild_jump(tree_exp_t* el, void* cl, Arena_T a)
{
    tree_stm_t* old_jump = cl;
    return tree_stm_jump(
            el, old_jump->tst_jump_num_labels, old_jump->tst_jump_labels, a);
}

static tree_stm_t* rebuild_cjump(tree_exp_t* el, void* cl, Arena_T a)
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
            old_cjump->tst_cjump_false, a);
}

static tree_stm_t* rebuild_move_temp_call(tree_exp_t* el, void* cl, Arena_T a)
{
    struct {
        tree_exp_t* temp;
        size_t call_size;
        tree_typ_t* result_type;
        void* ptr_map;
    } *cl_ = cl;

    var e = el;
    el = el->te_list;
    e->te_list = NULL;
    return tree_stm_move(
            cl_->temp,
            tree_exp_call(e, el, cl_->call_size, cl_->result_type,
                cl_->ptr_map, a), a);
}

static tree_stm_t* rebuild_move_temp(tree_exp_t* el, void* cl, Arena_T a)
{
    tree_exp_t* temp = cl;
    return tree_stm_move(temp, el, a);
}

static tree_stm_t* rebuild_move_mem(tree_exp_t* el, void* cl, Arena_T a)
{
    tree_exp_t* orig_dst = cl;
    var e = el;
    var b = el->te_list;
    e->te_list = NULL;
    return tree_stm_move(
            tree_exp_mem(e, orig_dst->te_size, orig_dst->te_type, a), b, a);
}

static tree_stm_t* rebuild_exp_call(tree_exp_t* el, void* cl, Arena_T a)
{
    struct {
        size_t call_size;
        tree_typ_t* result_type;
        void* ptr_map;
    } *cl_ = cl;

    var e = el;
    el = el->te_list;
    e->te_list = NULL;
    return tree_stm_exp(
            tree_exp_call(e, el, cl_->call_size, cl_->result_type,
                cl_->ptr_map, a), a);
}

static tree_stm_t* rebuild_exp(tree_exp_t* el, void* _cl, Arena_T a)
{
    return tree_stm_exp(el, a);
}

static tree_stm_t* rebuild_stm_other(tree_exp_t* _el, void* cl, Arena_T _a)
{
    return (tree_stm_t*)cl;
}

static tree_stm_t* do_stm(canon_info_t* info, tree_stm_t* s)
{
    switch (s->tst_tag) {
        case TREE_STM_SEQ:
            return seq(
                    do_stm(info, s->tst_seq_s1),
                    do_stm(info, s->tst_seq_s2), info->arena);
        case TREE_STM_JUMP:
            return reorder_stm(
                    info, s->tst_jump_dst, bsp_func(rebuild_jump, s));
        case TREE_STM_CJUMP:
        {
            tree_exp_t* el = tree_exp_append(
                    s->tst_cjump_lhs, s->tst_cjump_rhs);
            return reorder_stm(
                    info, el, bsp_func(rebuild_cjump, s));
        }
        case TREE_STM_MOVE:
        {
            if (s->tst_move_dst->te_tag == TREE_EXP_TEMP) {
                if (s->tst_move_exp->te_tag == TREE_EXP_CALL) {
                    struct {
                        tree_exp_t* temp;
                        size_t call_size;
                        tree_typ_t* result_type;
                        void* ptr_map;
                    } cl = {
                        .temp = s->tst_move_dst,
                        .call_size = s->tst_move_exp->te_size,
                        .result_type = s->tst_move_exp->te_type,
                        .ptr_map = s->tst_move_exp->te_ptr_map
                    };
                    var e = s->tst_move_exp->te_func;
                    var el = s->tst_move_exp->te_args;
                    return reorder_stm(
                            info,
                            tree_exp_append(e, el),
                            bsp_func(rebuild_move_temp_call, &cl));
                }
                return reorder_stm(
                        info,
                        s->tst_move_exp,
                        bsp_func(rebuild_move_temp, s->tst_move_dst));
            }
            if (s->tst_move_dst->te_tag == TREE_EXP_MEM) {
                var e = s->tst_move_dst->te_mem_addr;
                var b = s->tst_move_exp;
                return reorder_stm(
                        info,
                        tree_exp_append(e, b),
                        bsp_func(rebuild_move_mem, s->tst_move_dst));
            }
            if (s->tst_move_dst->te_tag == TREE_EXP_ESEQ) {
                var as_seq = tree_stm_seq(
                        s->tst_move_dst->te_eseq_stm,
                        tree_stm_move(
                            s->tst_move_dst->te_eseq_exp, s->tst_move_exp,
                            info->arena),
                        info->arena);
                return do_stm(info, as_seq);
            }
            // default case
            return reorder_stm(info, NULL, bsp_func(rebuild_stm_other, s));
        }
        case TREE_STM_EXP:
        {
            if (s->tst_exp->te_tag == TREE_EXP_CALL) {
                struct {
                    size_t _0;
                    tree_typ_t* _1;
                    void* _2;
                } cl = {
                    s->tst_exp->te_size,
                    s->tst_exp->te_type,
                    s->tst_exp->te_ptr_map,
                };
                var e = s->tst_exp->te_func;
                var el = s->tst_exp->te_args;
                return reorder_stm(
                        info,
                        tree_exp_append(e, el),
                        bsp_func(rebuild_exp_call, &cl));
            }
            var e = s->tst_exp;
            return reorder_stm(info, e, bsp_func(rebuild_exp, NULL));
        }
        case TREE_STM_LABEL:
            return reorder_stm(info, NULL, bsp_func(rebuild_stm_other, s));
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
static tree_stm_t* linearise(canon_info_t* info, tree_stm_t* s)
{
    return linear(do_stm(info, s), NULL);
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
    var final_node = &(blocks->bb_blocks);
    while (*final_node)
        final_node = &(*final_node)->bb_list;
    *final_node = block;
}

static tree_stm_t* unconditional_jump(sl_sym_t dst, Arena_T a)
{
    sl_sym_t* labels = Alloc(a, 1 * sizeof *labels);
    labels[0] = dst;
    return tree_stm_jump(tree_exp_name(dst, a), 1, labels, a);
}

/**
 * Convert a sequence of tree statements into blocks starting with a label
 * and ending with a jump or cjump
 */
/* stm list -> (stm list list * label) */
static basic_blocks_t
basic_blocks(canon_info_t* info, tree_stm_t* stmts)
{
    sl_sym_t done = temp_newlabel(info->temp_state);

    basic_block_t* curr_block = NULL;
    basic_blocks_t result = { .bb_end_label = done };

    if (stmts->tst_tag != TREE_STM_LABEL) {
        tree_stm_t* start_label =
            tree_stm_label(temp_newlabel(info->temp_state), info->arena);
        start_label->tst_list = stmts;
        stmts = start_label;
    }

    while (stmts) {
        // pop the top statement off the list
        tree_stm_t* s = stmts; stmts = stmts->tst_list; s->tst_list = NULL;

        // start a new block if necessary
        if (curr_block == NULL) {
            assert(s->tst_tag == TREE_STM_LABEL);
            curr_block = Alloc(info->scratch, sizeof *curr_block);
        }

        // append this statement to the current block
        curr_block->bb_stmts = tree_stm_append(curr_block->bb_stmts, s);

        // if this statement is jump, then we are going to end the current
        // block
        if (s->tst_tag == TREE_STM_JUMP || s->tst_tag == TREE_STM_CJUMP) {
            // well, if there is no label upcoming, then the following code is
            // dead
            while (stmts && stmts->tst_tag != TREE_STM_LABEL) {
                #ifndef NDEBUG
                    tree_printf(stderr, "deleting dead code: %S\n", stmts);
                #endif
                stmts = stmts->tst_list;
            }
            // now we will be ready to end the current block
        }

        // if the next statement is a label, end the current block
        if (stmts == NULL || stmts->tst_tag == TREE_STM_LABEL) {
            if (s->tst_tag != TREE_STM_JUMP && s->tst_tag != TREE_STM_CJUMP) {
                var j = unconditional_jump(
                    stmts ? stmts->tst_label : done, info->arena
                );
                curr_block->bb_stmts =
                    tree_stm_append(curr_block->bb_stmts, j);
            }
            bb_append_block(&result, curr_block);
            curr_block = NULL;
        }
    }
    // check we ended the final block
    assert(curr_block == NULL);

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

static trace_list_t* trace_list_new(trace_t* t, Arena_T ar)
{
    trace_list_t* tl = Alloc(ar, sizeof *tl);
    tl->tl_trace = t;
    return tl;
}
static trace_t* append_block_to_trace(trace_t* trace, basic_block_t* b)
{
    assert(b->bb_list == NULL);
    var end = &trace;
    while (*end)
        end = &(*end)->bb_list;
    *end = b;
    return trace;
}

static trace_list_t*
append_trace_to_trace_list(trace_list_t* hd, trace_t* t, Arena_T ar)
{
    var list_item = trace_list_new(t, ar);
    var end = &hd;
    while (*end)
        end = &(*end)->tl_list;
    *end = list_item;
    return hd;
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
        canon_info_t* info, tree_stm_t* result)
{
    for (var s = result; s->tst_list; s = s->tst_list) {
        var s1 = s->tst_list;
        if (s1->tst_tag == TREE_STM_CJUMP) {
            var s2 = s1->tst_list;
            bool s2_is_lbl = s2 && s2->tst_tag == TREE_STM_LABEL;
            if (s2_is_lbl && s1->tst_cjump_false == s2->tst_label) {
                // do nothing , we good!
            } else if (s2_is_lbl && s1->tst_cjump_true == s2->tst_label) {
                // invert the operation and flip the labels
                s->tst_list = tree_stm_cjump(
                        invert_relop(s1->tst_cjump_op),
                        s1->tst_cjump_lhs,
                        s1->tst_cjump_rhs,
                        s1->tst_cjump_false,
                        s1->tst_cjump_true,
                        info->arena);
                s->tst_list->tst_list = s2;
                // leak s1 ,lol
            } else { // neither the t or f label follows
                // add an unconditional jump
                sl_sym_t f0 = temp_newlabel(info->temp_state);
                s->tst_list = tree_stm_cjump(
                        s1->tst_cjump_op,
                        s1->tst_cjump_lhs,
                        s1->tst_cjump_rhs,
                        s1->tst_cjump_true,
                        f0,
                        info->arena);
                s->tst_list->tst_list = tree_stm_label(f0, info->arena);
                s->tst_list->tst_list->tst_list =
                    unconditional_jump(s1->tst_cjump_false, info->arena);
                s->tst_list->tst_list->tst_list->tst_list = s2;
            }
        }
    }
}

static tree_stm_t*
trace_schedule(canon_info_t* info, basic_blocks_t blocks)
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
            traces = append_trace_to_trace_list(traces, T, info->scratch);
    }

    Table_free(&table);

    // build a new list of basic blocks by following the traces

    arrtype(tree_stm_t*) stmts_in_order = {};

    for (var ti = traces; ti; ti = ti->tl_list) {
        for (var bb = ti->tl_trace; bb; bb = bb->bb_list) {
            for (var s = bb->bb_stmts; s; s = s->tst_list){
                // NOLINTNEXTLINE(bugprone-sizeof-expression)
                arrpush(&stmts_in_order, s);
            }
        }
    }
    // NOLINTNEXTLINE(bugprone-sizeof-expression)
    arrpush(&stmts_in_order, tree_stm_label(blocks.bb_end_label, info->arena));

    for (int i = 0; i < stmts_in_order.len - 1; i++) {
        stmts_in_order.data[i]->tst_list = stmts_in_order.data[i+1];
    }
    arrlast(stmts_in_order)->tst_list = NULL;
    tree_stm_t* result = stmts_in_order.data[0];
    arrfree(stmts_in_order);

    // remove unconditional jumps that are followed by their label
    while (remove_redundant_unconditional_jumps(result) > 0) {
        // ^------ side-effect in loop condition -----^
    }

    // rewrite cjumps so that their false label follows
    put_falses_after_cjumps(info, result);

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

    if (debug && err) {
        for (var s = stmts; s; s = s->tst_list) {
            tree_printf(stderr, "%S\n", s);
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
                if (!Table_get(t, s->tst_cjump_true)
                        && s->tst_cjump_true != blocks.bb_end_label) {
                    fprintf(stderr, "%s: missing %s label\n", check, s->tst_cjump_true);
                    err++;
                }
                if (!Table_get(t, s->tst_cjump_false)
                        && s->tst_cjump_false != blocks.bb_end_label) {
                    fprintf(stderr, "%s: missing %s label\n", check, s->tst_cjump_false);
                    err++;
                }
            } else if (s->tst_tag == TREE_STM_JUMP) {
                for (int i = 0 ; i < s->tst_jump_num_labels ; i++) {
                    var lbl = s->tst_jump_labels[i];
                    if (!Table_get(t, lbl) && lbl != blocks.bb_end_label){
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

void
canonicalise_tree(
        Arena_T arena, const target_t* target, temp_state_t* temp_state,
        sl_fragment_t* fragments)
{
    canon_info_t info = {
        .temp_state = temp_state,
        .target = target,
        .arena = arena,
        .scratch = Arena_new(),
    };

    for (var frag = fragments; frag; frag = frag->fr_list) {
        switch (frag->fr_tag) {
            case FR_CODE:
            {
                frag->fr_body = linearise(&info, frag->fr_body);
                verify_statements(frag->fr_body, "post-linearise");

                var blocks = basic_blocks(&info, frag->fr_body);
                verify_basic_blocks(blocks, "post-basic_blocks");

                frag->fr_body = trace_schedule(&info, blocks);
                verify_statements(frag->fr_body, "post-trace_schedule");
                break;
            }
            case FR_STRING:
            case FR_FRAME_MAP:
                continue;
        }
        Arena_free(info.scratch);
    }

    Arena_dispose(&info.scratch);
}
