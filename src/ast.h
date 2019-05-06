#ifndef __AST_H__
#define __AST_H__

#include <stdio.h> // FILE*
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
    int dl_var_id;
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
        SL_TYPE_FUNC,
    } ty_tag;
    union {
        sl_sym_t ty_name; // SL_TYPE_NAME
        sl_type_t* ty_pointee; // SL_TYPE_PTR, SL_TYPE_ARRAY
    };
};

struct sl_expr_t {
    enum {
        SL_EXPR_INT = 1,
        SL_EXPR_BOOL,
        SL_EXPR_VOID,
        SL_EXPR_BINOP,
        SL_EXPR_LET,
        SL_EXPR_CALL,
        SL_EXPR_NEW,
        SL_EXPR_VAR,
        SL_EXPR_RETURN,
        SL_EXPR_BREAK,
        SL_EXPR_LOOP,
        SL_EXPR_DEREF,
        SL_EXPR_ADDROF,
        SL_EXPR_MEMBER,
        SL_EXPR_IF,
    } ex_tag;
    int ex_op; // SL_TOK_... binop token
    int ex_line;
    sl_type_t* ex_type; // assigned by the type-checker

    union {
        struct {
            long long _value;
        } _const;

        struct {
            sl_expr_t* _left;
            sl_expr_t* _right;
        } _binop;

        struct {
            sl_sym_t _name;
            sl_type_t* _type;
            sl_expr_t* _init;
            int _id;
        } _let;

        struct {
            sl_sym_t _ref;
            sl_expr_t* _args;
        } _call;

        struct {
            sl_sym_t _ref;
            int _id;
        } _var;

        struct {
            sl_expr_t* _cond;
            sl_expr_t* _cons;
            sl_expr_t* _alt;
        } _if;

        struct {
            sl_expr_t* _struct;
            sl_sym_t _member;
        } _member;

    } ex_u;

    sl_expr_t* ex_list; // call args, or next expr in function body
    sl_expr_t* ex_link; // All allocated sl_expr_t nodes
};

#define ex_value        ex_u._const._value  // SL_EXPR_INT, SL_EXPR_BOOL
#define ex_left         ex_u._binop._left   // SL_EXPR_BINOP
#define ex_right        ex_u._binop._right  // SL_EXPR_BINOP
#define ex_name         ex_u._let._name     // SL_EXPR_LET
#define ex_type_ann     ex_u._let._type     // SL_EXPR_LET
#define ex_init         ex_u._let._init     // SL_EXPR_LET
#define ex_let_id       ex_u._let._id       // SL_EXPR_LET
#define ex_field_label  ex_u._let._name     // SL_EXPR_FIELD // TODO
#define ex_field_init   ex_u._let._init     // SL_EXPR_FIELD
#define ex_fn_name      ex_u._call._ref     // SL_EXPR_CALL
#define ex_fn_args      ex_u._call._args    // SL_EXPR_CALL
#define ex_new_ctor     ex_u._call._ref     // SL_EXPR_NEW
#define ex_new_args     ex_u._call._args    // SL_EXPR_NEW
#define ex_var          ex_u._var._ref      // SL_EXPR_VAR
#define ex_var_id       ex_u._var._id       // SL_EXPR_VAR
#define ex_ret_arg      ex_u._binop._left   // SL_EXPR_RETURN
#define ex_loop_body    ex_u._binop._left   // SL_EXPR_LOOP
#define ex_deref_arg    ex_u._binop._left   // SL_EXPR_DEREF
#define ex_addrof_arg   ex_u._binop._left   // SL_EXPR_ADDROF
#define ex_composite    ex_u._member._struct // SL_EXPR_MEMBER
#define ex_member       ex_u._member._member // SL_EXPR_MEMBER
#define ex_if_cond      ex_u._if._cond      // SL_EXPR_IF
#define ex_if_cons      ex_u._if._cons      // SL_EXPR_IF
#define ex_if_alt       ex_u._if._alt       // SL_EXPR_IF

// AST Node Constructors

sl_decl_t* dl_struct(sl_sym_t name, sl_decl_t* params);
sl_decl_t* dl_func(sl_sym_t name, sl_decl_t* params, sl_type_t* ret_type, sl_expr_t* body);
sl_decl_t* dl_param(sl_sym_t name, sl_type_t* type);
sl_decl_t* dl_append(sl_decl_t* dln, sl_decl_t* to_append);

sl_type_t* ty_type_name(sl_sym_t name);
sl_type_t* ty_type_pointer(sl_type_t* pointee);
sl_type_t* ty_type_func();

sl_expr_t* ex_append(sl_expr_t* exn, sl_expr_t* to_append);
sl_expr_t* sl_expr_int(int value);
sl_expr_t* sl_expr_bool(_Bool value);
sl_expr_t* sl_expr_void();
sl_expr_t* sl_expr_binop(int op, sl_expr_t* left, sl_expr_t* right);
sl_expr_t* sl_expr_let(sl_sym_t name, sl_type_t* type, sl_expr_t* init);
sl_expr_t* sl_expr_call(sl_sym_t fn_name, sl_expr_t* args);
sl_expr_t* sl_expr_new(sl_sym_t struct_name, sl_expr_t* args);
sl_expr_t* sl_expr_var(sl_sym_t name);
sl_expr_t* sl_expr_return(sl_expr_t* return_value_expr);
sl_expr_t* sl_expr_break();
sl_expr_t* sl_expr_loop(sl_expr_t* smts);
sl_expr_t* sl_expr_deref(sl_expr_t* expr);
sl_expr_t* sl_expr_addrof(sl_expr_t* expr);
sl_expr_t* sl_expr_member(sl_expr_t* expr, sl_sym_t member);
sl_expr_t* sl_expr_if(sl_expr_t* cond, sl_expr_t* cons, sl_expr_t* alt);

// AST Printing functions

void ast_printf(FILE* out, const char* fmt, ...);

void dl_print(FILE* out, const sl_decl_t* decl);
void ex_print(FILE* out, const sl_expr_t* expr);
void ty_print(FILE* out, const sl_type_t* expr);

// Helper functions

int dl_struct_num_fields(sl_decl_t* struct_decl) __attribute__((pure));
int dl_func_num_params(sl_decl_t* func_decl) __attribute__((pure));

// TODO
//unsigned long ty_hash(sl_type_t* type);
int ty_cmp(sl_type_t* t1, sl_type_t* t2) __attribute__((nonnull(1, 2)));

#endif /* __AST_H__ */
