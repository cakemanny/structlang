#include <stdlib.h>
#include <stdio.h> // perror
#include <string.h> // memset
#include "ast.h"

#define check_mem(ptr) do { \
        if ((ptr) == NULL) { perror("out of memory"); abort(); } \
    } while (0)

/* Line number from flex lexer */
extern int yylineno;

static sl_decl_t* dl_alloc(int tag)
{
    sl_decl_t* node = malloc(sizeof *node);
    check_mem(node);

    node->dl_tag = tag;
    node->dl_line = yylineno;
    node->dl_params = NULL;
    node->dl_type = NULL;
    node->dl_body = NULL;
    node->dl_list = NULL;
    node->dl_link = NULL; // TODO: should be added to allocator's list
    return node;
}

sl_decl_t* dl_struct(sl_sym_t name, sl_decl_t* params)
{
    sl_decl_t* node = dl_alloc(SL_DECL_STRUCT);
    node->dl_name = name;
    node->dl_params = params;
    return node;
}

sl_decl_t* dl_func(sl_sym_t name, sl_decl_t* params, sl_type_t* ret_type, sl_expr_t* body)
{
    sl_decl_t* node = dl_alloc(SL_DECL_FUNC);
    node->dl_name = name;
    node->dl_params = params;
    node->dl_type = ret_type;
    node->dl_body = body;
    return node;
}

sl_decl_t* dl_param(sl_sym_t name, sl_type_t* type)
{
    sl_decl_t* node = dl_alloc(SL_DECL_PARAM);
    node->dl_name = name;
    node->dl_type = type;
    return node;
}

sl_decl_t* dl_append(sl_decl_t* dln, sl_decl_t* to_append)
{
    if (!dln)
        return to_append;

    sl_decl_t* final_node = dln;
    while (final_node->dl_list)
        final_node = final_node->dl_list;
    final_node->dl_list = to_append;

    return dln;
}

static sl_type_t* ty_alloc(int tag)
{
    sl_type_t* node = malloc(sizeof *node);
    check_mem(node);

    node->ty_tag = tag;

    memset(&node->ty_u, 0, sizeof node->ty_u);

    return node;
}

sl_type_t* ty_type_name(sl_sym_t name)
{
    sl_type_t* node = ty_alloc(SL_TYPE_NAME);
    node->ty_name = name;
    return node;
}

sl_type_t* ty_type_pointer(sl_type_t* pointee)
{
    sl_type_t* node = ty_alloc(SL_TYPE_PTR);
    node->ty_pointee = pointee;
    return node;
}

static sl_expr_t* ex_alloc(int tag)
{
    sl_expr_t* node = malloc(sizeof *node);
    check_mem(node);

    node->ex_op = 0;
    node->ex_line = yylineno;
    node->ex_type = NULL; // assigned by type-checker
    memset(&node->ex_u, 0, sizeof node->ex_u);
    node->ex_list = NULL;
    node->ex_link = NULL;
    return node;
}

sl_expr_t* ex_append(sl_expr_t* exn, sl_expr_t* to_append)
{
    if (!exn)
        return to_append;

    sl_expr_t* final_node = exn;
    while (final_node->ex_list)
        final_node = final_node->ex_list;
    final_node->ex_list = to_append;

    return exn;
}

sl_expr_t* sl_expr_int(int value)
{
    sl_expr_t* node = ex_alloc(SL_EXPR_INT);
    node->ex_value = value;
    return node;
}

sl_expr_t* sl_expr_binop(int op, sl_expr_t* left, sl_expr_t* right)
{
    sl_expr_t* node = ex_alloc(SL_EXPR_BINOP);
    node->ex_op = op;
    node->ex_left = left;
    node->ex_right = right;
    return node;
}

sl_expr_t* sl_expr_let(sl_sym_t name, sl_type_t* type, sl_expr_t* init)
{
    sl_expr_t* node = ex_alloc(SL_EXPR_LET);
    node->ex_name = name;
    node->ex_type = type;
    node->ex_init = init;
    return node;
}

sl_expr_t* sl_expr_call(sl_sym_t fn_name, sl_expr_t* args)
{
    sl_expr_t* node = ex_alloc(SL_EXPR_CALL);
    node->ex_fn_name = fn_name;
    node->ex_fn_args = args;
    return node;
}

sl_expr_t* sl_expr_new(sl_sym_t struct_name, sl_expr_t* args)
{
    sl_expr_t* node = ex_alloc(SL_EXPR_NEW);
    node->ex_new_ctor = struct_name;
    node->ex_new_args = args;
    return node;
}

sl_expr_t* sl_expr_var(sl_sym_t name)
{
    sl_expr_t* node = ex_alloc(SL_EXPR_VAR);
    node->ex_var = name;
    return node;
}

sl_expr_t* sl_expr_return(sl_expr_t* return_value_expr)
{
    sl_expr_t* node = ex_alloc(SL_EXPR_RETURN);
    node->ex_ret_arg = return_value_expr;
    return node;
}

sl_expr_t* sl_expr_break()
{
    return ex_alloc(SL_EXPR_BREAK);
}

sl_expr_t* sl_expr_loop(sl_expr_t* stmts)
{
    sl_expr_t* node = ex_alloc(SL_EXPR_LOOP);
    node->ex_left = stmts;
    return node;
}

sl_expr_t* sl_expr_deref(sl_expr_t* expr)
{
    sl_expr_t* node = ex_alloc(SL_EXPR_DEREF);
    node->ex_left = expr;
    return node;
}
