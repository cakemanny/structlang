#include <assert.h>
#include "symbols.h"
#include "ast.h"
#include "grammar.tab.h"
#include "activation.h"
#include "semantics.h"

#define EX_LIST_IT(it, head) sl_expr_t* it = (head); it; it = it->ex_list
#define DL_LIST_IT(it, head) sl_decl_t* it = (head); it; it = it->dl_list
#define VAR(v, x) typeof(x) v = (x)

typedef struct rewrite_info_t {
    sl_decl_t* program;
    int next_temp_id; // different to the temps in temp.h
} rewrite_info_t;

static sl_decl_t* lookup_struct_decl(sl_decl_t* program, sl_sym_t name)
{
    sl_decl_t* x;
    for (x = program; x; x = x->dl_list) {
        if (x->dl_tag == SL_DECL_STRUCT) {
            if (x->dl_name == name) {
                return x;
            }
        }
    }
    return NULL;
}

static void rewrite_decompose_equal_expr(rewrite_info_t* info, sl_expr_t* expr)
{
#define recur(e) rewrite_decompose_equal_expr(info, e)
    switch (expr->ex_tag) {
        /* interesting case */
        case SL_EXPR_BINOP:
            if (expr->ex_op == SL_TOK_EQ || expr->ex_op == SL_TOK_NEQ) {
                VAR(left_type, expr->ex_left->ex_type);
                if (size_of_type(info->program, left_type) > ac_word_size) {
                    assert(expr->ex_type->ty_tag== SL_TYPE_NAME &&
                            expr->ex_type->ty_name == symbol("bool"));
                    VAR(bool_type, expr->ex_type);
                    // must be a struct...
                    assert(left_type->ty_tag == SL_TYPE_NAME);
                    sl_decl_t* struct_decl =
                        lookup_struct_decl(info->program, left_type->ty_name);
                    assert(struct_decl);

                    // do we think these must always be lvalues?
                    assert(sem_is_lvalue(expr->ex_left));
                    assert(sem_is_lvalue(expr->ex_right));

                    _Bool is_eq = (expr->ex_op == SL_TOK_EQ);
                    VAR(comb_op , is_eq ? SL_TOK_LAND : SL_TOK_LOR);

                    // TODO: factor into function
                    sl_expr_t* head = NULL;
                    {
                        VAR(field, struct_decl->dl_params);
                        VAR(left_access,
                                sl_expr_member(expr->ex_left, field->dl_name));
                        left_access->ex_type = field->dl_type;

                        VAR(right_access,
                                sl_expr_member(expr->ex_right, field->dl_name));
                        right_access->ex_type = field->dl_type;

                        // now recurse in case, in case those are structs.
                        recur(left_access);
                        recur(right_access);

                        sl_expr_t* new_cmp =
                            sl_expr_binop(expr->ex_op, left_access, right_access);
                        new_cmp->ex_type = bool_type;
                        head = new_cmp;
                    }
                    for (DL_LIST_IT(field, struct_decl->dl_params->dl_list)) {
                        VAR(left_access,
                                sl_expr_member(expr->ex_left, field->dl_name));
                        left_access->ex_type = field->dl_type;

                        VAR(right_access,
                                sl_expr_member(expr->ex_right, field->dl_name));
                        right_access->ex_type = field->dl_type;

                        // now recurse in case, in case those are structs.
                        recur(left_access);
                        recur(right_access);

                        sl_expr_t* new_cmp =
                            sl_expr_binop(expr->ex_op, left_access, right_access);
                        new_cmp->ex_type = bool_type;

                        head = sl_expr_binop(comb_op, head, new_cmp);
                        head->ex_type = bool_type;
                    }
                    // patch up the expression
                    expr->ex_op = head->ex_op;
                    expr->ex_left = head->ex_left;
                    expr->ex_right = head->ex_right;
                    // TODO: free head?
                }
            }
            recur(expr->ex_left);
            recur(expr->ex_right);
            return;
        /* recursive cases */
        case SL_EXPR_INT: /* fall through */
        case SL_EXPR_BOOL: /* fall through */
        case SL_EXPR_VOID:
            return;
        case SL_EXPR_LET:
            recur(expr->ex_init);
            return;
        case SL_EXPR_CALL:
            for (EX_LIST_IT(arg, expr->ex_fn_args)) {
                recur(arg);
            }
            return;
        case SL_EXPR_NEW:
            for (EX_LIST_IT(arg, expr->ex_new_args)) {
                recur(arg);
            }
            return;
        case SL_EXPR_VAR:
            return;
        case SL_EXPR_RETURN:
            recur(expr->ex_ret_arg);
            return;
        case SL_EXPR_BREAK:
            return;
        case SL_EXPR_LOOP:
            for (EX_LIST_IT(stmt, expr->ex_loop_body)) {
                recur(stmt);
            }
            return;
        case SL_EXPR_DEREF:
            recur(expr->ex_deref_arg);
            return;
        case SL_EXPR_ADDROF:
            recur(expr->ex_addrof_arg);
            return;
        case SL_EXPR_MEMBER:
            recur(expr->ex_composite);
            return;
        case SL_EXPR_IF:
            recur(expr->ex_if_cond);
            recur(expr->ex_if_cons);
            recur(expr->ex_if_alt);
            return;
    }
    assert(0);
}

static void rewrite_decompose_equal_decl(rewrite_info_t* info, sl_decl_t* decl)
{
    assert(decl->dl_tag == SL_DECL_FUNC);
    for (EX_LIST_IT(e, decl->dl_body)) {
        rewrite_decompose_equal_expr(info, e);
    }
}

/*
 * Given
 *  struct A { a: int; b: int; };
 *  let x: A = ...; let y: A = ...
 * Turn
 *  x == y into x.a == y.a && x.b == y.b
 */
void rewrite_decompose_equal(sl_decl_t* program)
{
    rewrite_info_t info = { .program = program };
    for (DL_LIST_IT(decl, program)) {
        if (decl->dl_tag == SL_DECL_FUNC) {
            rewrite_decompose_equal_decl(&info, decl);
        }
    }
}
