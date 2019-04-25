#ifndef __AST_H__
#define __AST_H__

#include <stdio.h>
#include "symbols.h"

struct sl_expr_t;
typedef struct sl_expr_t sl_expr_t;

struct sl_decl_t;
typedef struct sl_decl_t sl_decl_t;

struct sl_type_t;
typedef struct sl_type_t sl_type_t;

struct sl_decl_t {
    enum {
        SL_DECL_STRUCT = 1,
        SL_DECL_FUNC,
        SL_DECL_PARAM,
    } dl_tag;
    int dl_line;
    sl_sym_t dl_name;
    sl_decl_t* dl_params;   // SL_DECL_STRUCT, SL_DECL_FUNC
    sl_type_t* dl_type;     // SL_DECL_FUNC, SL_DECL_PARAM
    sl_expr_t* dl_body;     // SL_DECL_FUNC

    sl_decl_t* dl_list; // next declaration in program
    sl_decl_t* dl_link; // all sl_decl_t nodes
};

/* These need to somehow be interned / refer to a table of type declarations */
struct sl_type_t {
    enum {
        SL_TYPE_NAME = 1,
        SL_TYPE_PTR,
        SL_TYPE_ARRAY,
    } ty_tag;
    int ty_line;
    union {
        sl_sym_t _name; // SL_TYPE_NAME
        sl_type_t* _pointee; // SL_TYPE_PTR, SL_TYPE_ARRAY
    } ty_u;
};

#define ty_name     ty_u._name
#define ty_pointee  ty_u._pointee

struct sl_expr_t {
    enum {
        SL_EXPR_INT = 1,
        SL_EXPR_BINOP,
        SL_EXPR_LET,
        SL_EXPR_CALL,
        SL_EXPR_NEW,
        SL_EXPR_VAR,
        SL_EXPR_RETURN,
        SL_EXPR_BREAK,
        SL_EXPR_LOOP,
        SL_EXPR_DEREF
    } ex_tag;
    int ex_op; // SL_TOK_... binop token
    int ex_line;
    sl_type_t* ex_type; // assigned by the type-checker

    union {
        struct {
            long long _value;
            sl_sym_t _label; // label of constructor args
        } _const;

        struct {
            sl_expr_t* _left;
            sl_expr_t* _right;
        } _binop; /* or return: SL_EXPR_BINOP, SL_EXPR_RETURN (_left only)
                     SL_EXPR_LOOP (_left = expr list)
                   */
        struct {
            sl_sym_t _name;
            sl_type_t* _type;
            sl_expr_t* _init;
        } _let;

        struct {
            sl_sym_t _ref;
            sl_expr_t* _args;
        } _call; // SL_EXPR_CALL, SL_EXPR_NEW, SL_EXPR_VAR

    } ex_u;

    sl_expr_t* ex_list; // call args, or next expr in function body
    sl_expr_t* ex_link; // All allocated sl_expr_t nodes
};

#define ex_value        ex_u._const._value  // SL_EXPR_INT
#define ex_label        ex_u._const._label
#define ex_left         ex_u._binop._left   // SL_EXPR_BINOP
#define ex_right        ex_u._binop._right  // SL_EXPR_BINOP
#define ex_name         ex_u._let._name     // SL_EXPR_LET
#define ex_type_ann     ex_u._let._type     // SL_EXPR_LET
#define ex_init         ex_u._let._init     // SL_EXPR_LET
#define ex_fn_name      ex_u._call._ref     // SL_EXPR_CALL
#define ex_fn_args      ex_u._call._args    // SL_EXPR_CALL
#define ex_new_ctor     ex_u._call._ref     // SL_EXPR_NEW
#define ex_new_args     ex_u._call._args    // SL_EXPR_NEW
#define ex_var          ex_u._call._ref     // SL_EXPR_VAR
#define ex_ret_arg      ex_u._binop._left   // SL_EXPR_RETURN
#define ex_loop_body    ex_u._binop._left   // SL_EXPR_LOOP
#define ex_deref_arg    ex_u._binop._left   // SL_EXPR_DEREF

sl_decl_t* dl_struct(sl_sym_t name, sl_decl_t* params);
sl_decl_t* dl_func(sl_sym_t name, sl_decl_t* params, sl_type_t* ret_type, sl_expr_t* body);
sl_decl_t* dl_param(sl_sym_t name, sl_type_t* type);
sl_decl_t* dl_append(sl_decl_t* dln, sl_decl_t* to_append);

sl_type_t* ty_type_name(sl_sym_t name);
sl_type_t* ty_type_pointer(sl_type_t* pointee);

sl_expr_t* ex_append(sl_expr_t* exn, sl_expr_t* to_append);
sl_expr_t* sl_expr_int(int value);
sl_expr_t* sl_expr_binop(int op, sl_expr_t* left, sl_expr_t* right);
sl_expr_t* sl_expr_let(sl_sym_t name, sl_type_t* type, sl_expr_t* init);
sl_expr_t* sl_expr_call(sl_sym_t fn_name, sl_expr_t* args);
sl_expr_t* sl_expr_new(sl_sym_t struct_name, sl_expr_t* args);
sl_expr_t* sl_expr_var(sl_sym_t name);
sl_expr_t* sl_expr_return(sl_expr_t* return_value_expr);
sl_expr_t* sl_expr_break();
sl_expr_t* sl_expr_loop(sl_expr_t* smts);
sl_expr_t* sl_expr_deref(sl_expr_t* expr);

void ast_printf(FILE* out, const char* fmt, ...);

void dl_print(FILE* out, const sl_decl_t* decl);
void ex_print(FILE* out, const sl_expr_t* expr);
void ty_print(FILE* out, const sl_type_t* expr);

#endif /* __AST_H__ */
