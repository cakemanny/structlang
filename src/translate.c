#include "translate.h"
#include <assert.h> /* assert */
#include "mem.h" // xmalloc
#include "grammar.tab.h"

label_bifunc_t label_bifunc(
        tree_stm_t* (*fn)(sl_sym_t, sl_sym_t, void* cl),
        void* cl)
{
    label_bifunc_t bifunc = { .lbf_fn = fn, .lbf_cl = cl };
    return bifunc;
}

static tree_stm_t* lbf_call(label_bifunc_t lbf, sl_sym_t t, sl_sym_t f)
{
    return lbf.lbf_fn(t, f, lbf.lbf_cl);
}

translate_exp_t* translate_ex(tree_exp_t* e)
{
    translate_exp_t* result = xmalloc(sizeof *result);
    result->tr_exp_tag = TR_EXP_EX;
    result->tr_exp_ex = e;
    return result;
}

translate_exp_t* translate_nx(tree_stm_t* s)
{
    translate_exp_t* result = xmalloc(sizeof *result);
    result->tr_exp_tag = TR_EXP_NX;
    result->tr_exp_nx = s;
    return result;
}

translate_exp_t* translate_cx(label_bifunc_t genstm)
{
    translate_exp_t* result = xmalloc(sizeof *result);
    result->tr_exp_tag = TR_EXP_CX;
    result->tr_exp_cx = genstm;
    return result;
}

tree_exp_t* translate_un_ex(temp_state_t* ts, translate_exp_t* ex)
{
    switch (ex->tr_exp_tag) {
        case TR_EXP_EX:
            return ex->tr_exp_ex;
        case TR_EXP_NX:
            return tree_exp_eseq(ex->tr_exp_nx, tree_exp_const(0));
        case TR_EXP_CX:
        {
            temp_t r = temp_newtemp(ts);
            sl_sym_t t = temp_newlabel(ts);
            sl_sym_t f = temp_newlabel(ts);

            return tree_exp_eseq(
                    tree_stm_seq(
                    tree_stm_seq(
                    tree_stm_seq(
                    tree_stm_seq(
                        tree_stm_move(tree_exp_temp(r), tree_exp_const(1))
                        ,
                        lbf_call(ex->tr_exp_cx, t, f) /* genstm */
                        ),
                        tree_stm_label(f)
                        ),
                        tree_stm_move(tree_exp_temp(r), tree_exp_const(0))
                        ),
                        tree_stm_label(t)
                        ),
                        tree_exp_temp(r)
                        );
        }
    }
}

tree_stm_t* translate_un_nx(temp_state_t* ts, translate_exp_t* ex)
{
    switch (ex->tr_exp_tag) {
        case TR_EXP_EX:
            return tree_stm_exp(ex->tr_exp_ex);
        case TR_EXP_NX:
            return ex->tr_exp_nx;
        case TR_EXP_CX:
        {
            // Just evaluate the conditional and continue...
            // Not sure if it's legal to end with a label...
            // maybe we need a post-label no-op?
            sl_sym_t dst = temp_newlabel(ts);
            return tree_stm_seq(
                lbf_call(ex->tr_exp_cx, dst, dst), /* genstm */
                tree_stm_label(dst)
            );
        }
    }
}

static tree_stm_t* unconditional_jump(sl_sym_t dst)
{
    sl_sym_t* labels = xmalloc(1 * sizeof * labels);
    labels[0] = dst;
    return tree_stm_jump(tree_exp_name(dst), 1, labels);
}

static tree_stm_t* always_true(sl_sym_t t, sl_sym_t f, void* cl)
{
    return unconditional_jump(t);
}

static tree_stm_t* always_false(sl_sym_t t, sl_sym_t f, void* cl)
{
    return unconditional_jump(f);
}

static tree_stm_t* jump_not_zero(sl_sym_t t, sl_sym_t f, void* cl)
{
    return tree_stm_cjump(
            TREE_RELOP_NE, tree_exp_const(0), (tree_exp_t*)cl, t, f);
}

label_bifunc_t translate_un_cx(translate_exp_t* ex)
{
    // Treat const 1 and const 0 specially he said
    switch (ex->tr_exp_tag) {
        case TR_EXP_EX:
        {
            tree_exp_t* e = ex->tr_exp_ex;
            switch (ex->tr_exp_ex->te_tag) {
                case TREE_EXP_CONST:
                    if (e->te_const == 0) {
                        return label_bifunc(always_false, NULL);
                    }
                    return label_bifunc(always_true, NULL);
                case TREE_EXP_NAME:
                case TREE_EXP_TEMP:
                case TREE_EXP_BINOP:
                case TREE_EXP_MEM:
                case TREE_EXP_CALL:
                case TREE_EXP_ESEQ:
                    return label_bifunc(jump_not_zero, e);
            }
            assert(0 && "missing case");
        }
        case TR_EXP_NX:
            assert(0 && "impossible case");
        case TR_EXP_CX:
            return ex->tr_exp_cx;
    }
}

/* forward declaration so we can be recursive  */
static translate_exp_t* translate_expr(
        temp_state_t* temp_state, ac_frame_t* frame, sl_expr_t* expr);

static translate_exp_t* translate_expr_var(
        temp_state_t* temp_state, ac_frame_t* frame, sl_expr_t* expr)
{
    struct ac_frame_var* frame_var = NULL;
    for (frame_var = frame->ac_frame_vars; frame_var;
            frame_var = frame_var->acf_list) {
        if (expr->ex_var_id == frame_var->acf_var_id) {
            break;
        }
    }
    assert(frame_var);

    tree_exp_t* result = tree_exp_mem(
        tree_exp_binop(
            TREE_BINOP_PLUS,
            tree_exp_temp(frame->acf_regs->acr_fp), // rbp
            tree_exp_const(frame_var->acf_offset)
        )
    );
    return translate_ex(result);
}

static translate_exp_t* translate_expr_int(
        temp_state_t* temp_state, ac_frame_t* frame, sl_expr_t* expr)
{
    tree_exp_t* result = tree_exp_const(expr->ex_value);
    return translate_ex(result);
}

static translate_exp_t* translate_expr_bool(
        temp_state_t* temp_state, ac_frame_t* frame, sl_expr_t* expr)
{
    tree_exp_t* result = tree_exp_const(expr->ex_value);
    return translate_ex(result);
}

static translate_exp_t* translate_expr_binop(
        temp_state_t* temp_state, ac_frame_t* frame, sl_expr_t* expr)
{
    translate_exp_t* lhs = translate_expr(temp_state, frame, expr->ex_left);
    translate_exp_t* rhs = translate_expr(temp_state, frame, expr->ex_right);
    // This freeing seems quite risky!
    tree_exp_t* lhe = translate_un_ex(temp_state, lhs); free(lhs); lhs = NULL;
    tree_exp_t* rhe = translate_un_ex(temp_state, rhs); free(rhs); rhs = NULL;

    // break out of here and compose some branches
    switch (expr->ex_op) {
        case SL_TOK_LOR:
        case SL_TOK_LAND:
        default: break;
    }

    if (expr->ex_op == SL_TOK_EQ || expr->ex_op == SL_TOK_NEQ) {
        // if the types of the things being compared is > 1 word, then we
        // need to rewrite it, as and AND of comparisons
        // might need to do this before the subexpression conversion
        // solve using a rewrite layer
    }

    int relop = -1;
    switch (expr->ex_op) {
        // what about the size of the operands?
        case SL_TOK_EQ: relop = TREE_RELOP_EQ; break;
        case SL_TOK_NEQ: relop = TREE_RELOP_NE; break;
        case '<': relop = TREE_RELOP_LT; break;
        case '>': relop = TREE_RELOP_GT; break;
        case SL_TOK_LE: relop = TREE_RELOP_LE; break;
        case SL_TOK_GE: relop = TREE_RELOP_GE; break;
        // missing: all the unsigned comparisons
        default: break;
    }
    if (relop != -1) {
        // break off here and do a cjump node
    }

    int op = -1;
    switch (expr->ex_op) {
        case '+': op = TREE_BINOP_PLUS; break;
        case '-': op = TREE_BINOP_MINUS; break;
        case '*': op = TREE_BINOP_MUL; break;
        case '/': op = TREE_BINOP_DIV; break;
        case '&': op = TREE_BINOP_AND; break;
        case '|': op = TREE_BINOP_OR; break;
        case '^': op = TREE_BINOP_XOR; break;
        case SL_TOK_LSH: op = TREE_BINOP_LSHIFT; break;
        case SL_TOK_RSH: op = TREE_BINOP_RSHIFT; break;
        // missing: TREE_BINOP_ARSHIFT
    }
    assert(op != -1);

    tree_exp_t* result = tree_exp_binop(op, lhe, rhe);
    return translate_ex(result);
}

static translate_exp_t* translate_expr(temp_state_t* temp_state, ac_frame_t* frame, sl_expr_t* expr)
{
    switch (expr->ex_tag)
    {
        case SL_EXPR_VAR:
            return translate_expr_var(temp_state, frame, expr);
        case SL_EXPR_INT:
            return translate_expr_int(temp_state, frame, expr);
        case SL_EXPR_BOOL:
            return translate_expr_bool(temp_state, frame, expr);
        case SL_EXPR_VOID:
            // TODO: should we ever get these?
            return NULL;
        case SL_EXPR_BINOP:
            return translate_expr_binop(temp_state, frame, expr);
        case SL_EXPR_LET:
        case SL_EXPR_NEW:
        case SL_EXPR_CALL:
        case SL_EXPR_RETURN:
        case SL_EXPR_BREAK:
        case SL_EXPR_LOOP:
        case SL_EXPR_DEREF:
        case SL_EXPR_ADDROF:
        case SL_EXPR_MEMBER:
        case SL_EXPR_IF:
            // TODO
            return NULL;
    }
}

void translate_decl(temp_state_t* temp_state, ac_frame_t* frame, sl_decl_t* decl)
{
    for (sl_expr_t* e = decl->dl_body; e; e = e->ex_list) {
        // TODO something with this's returns
        translate_expr(temp_state, frame, e);
    }
    // TODO: track the last expression and turn into a return?
}


void translate_program(
        temp_state_t* temp_state, sl_decl_t* program, ac_frame_t* frames)
{
    // return some sort of list of functions, with each carrying a reference
    // to the activation record, and to the IR representation

    sl_decl_t* d;
    ac_frame_t* f;
    for (d = program, f = frames; d; d = d->dl_list, f = f->acf_link) {
        assert(f); // d => f
        if (d->dl_tag == SL_DECL_FUNC) {
            // TODO something with this's returns
            translate_decl(temp_state, f, d);
        }
    }
    assert(!f);
}
