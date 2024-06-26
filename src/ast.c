#include <stdlib.h>
#include <stdio.h> // perror, fprintf, ...
#include <stdarg.h> // va_start, va_arg, va_end
#include <string.h> // memset
#include <assert.h> // assert
#include "ast.h"
#include "interfaces/arena.h"
#include "grammar.tab.h"

#define Alloc(arena, size) Arena_alloc(arena, size, __FILE__, __LINE__)

/* Line number from flex lexer */
extern int yylineno;

Arena_T ast_arena = NULL; // The current arena for allocating nodes


static sl_decl_t* dl_alloc(int tag)
{
    sl_decl_t* node = Alloc(ast_arena, sizeof *node);

    node->dl_tag = tag;
    node->dl_line = yylineno;
    node->dl_var_id = 0;
    node->dl_params = NULL;
    node->dl_type = NULL;
    node->dl_body = NULL;
    node->dl_list = NULL;
    node->dl_last = NULL;
    return node;
}

sl_decl_t* dl_struct(sl_sym_t name, sl_decl_t* params)
{
    sl_decl_t* node = dl_alloc(SL_DECL_STRUCT);
    node->dl_name = name;
    node->dl_params = params;
    return node;
}

sl_decl_t* dl_func(
        sl_sym_t name, sl_decl_t* params, sl_type_t* ret_type, sl_expr_t* body)
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

    sl_decl_t* final_node = (dln->dl_last) ? dln->dl_last : dln;
    while (final_node->dl_list)
        final_node = final_node->dl_list;
    final_node->dl_list = to_append;

    // stash the end node for O(1) append
    dln->dl_last = to_append;

    return dln;
}

static sl_type_t* ty_alloc(Arena_T a, int tag)
{
    sl_type_t* node = Alloc(a, sizeof *node);

    node->ty_tag = tag;
    node->ty_size = -1;
    node->ty_alignment = -1;
    node->ty_decl = NULL;

    return node;
}

sl_type_t* ty_type_name(Arena_T a, sl_sym_t name)
{
    sl_type_t* node = ty_alloc(a, SL_TYPE_NAME);
    node->ty_name = name;
    return node;
}

sl_type_t* ty_type_pointer(Arena_T a, sl_type_t* pointee)
{
    sl_type_t* node = ty_alloc(a, SL_TYPE_PTR);
    node->ty_pointee = pointee;
    return node;
}

sl_type_t* ty_type_func(Arena_T a)
{
    return ty_alloc(a, SL_TYPE_FUNC);
}

static sl_expr_t* ex_alloc(int tag)
{
    sl_expr_t* node = Alloc(ast_arena, sizeof *node);

    node->ex_tag = tag;
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

sl_expr_t* sl_expr_bool(bool value)
{
    sl_expr_t* node = ex_alloc(SL_EXPR_BOOL);
    node->ex_value = value;
    return node;
}

sl_expr_t* sl_expr_void()
{
    return ex_alloc(SL_EXPR_VOID);
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
    node->ex_type_ann = type;
    node->ex_init = init;
    node->ex_let_id = 0;
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
    node->ex_var_id = 0;
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
    node->ex_loop_body = stmts;
    return node;
}

sl_expr_t* sl_expr_deref(sl_expr_t* expr)
{
    sl_expr_t* node = ex_alloc(SL_EXPR_DEREF);
    node->ex_deref_arg = expr;
    return node;
}

sl_expr_t* sl_expr_addrof(sl_expr_t* expr)
{
    sl_expr_t* node = ex_alloc(SL_EXPR_ADDROF);
    node->ex_addrof_arg = expr;
    return node;
}

sl_expr_t* sl_expr_member(sl_expr_t* expr, sl_sym_t member)
{
    sl_expr_t* node = ex_alloc(SL_EXPR_MEMBER);
    node->ex_composite = expr;
    node->ex_member = member;
    return node;
}

sl_expr_t* sl_expr_if(sl_expr_t* cond, sl_expr_t* cons, sl_expr_t* alt)
{
    sl_expr_t* node = ex_alloc(SL_EXPR_IF);
    node->ex_if_cond = cond;
    node->ex_if_cons = cons;
    node->ex_if_alt = alt;
    return node;
}

// Print AST

void dl_list_print(FILE* out, const sl_decl_t* decl)
{
    const char* prefix = "";
    for (; decl; decl = decl->dl_list) {
        fprintf(out, "%s", prefix);
        dl_print(out, decl);
        prefix = " ";
    }
}

void ex_list_print(FILE* out, const sl_expr_t* expr)
{
    const char* prefix = "";
    for (; expr; expr = expr->ex_list) {
        fprintf(out, "%s", prefix);
        ex_print(out, expr);
        prefix = " ";
    }
}

void dl_print(FILE* out, const sl_decl_t* decl)
{
    switch (decl->dl_tag) {
        case SL_DECL_STRUCT: {
            fprintf(out, "(struct %s (", decl->dl_name);
            dl_list_print(out, decl->dl_params);
            fprintf(out, "))");
            return;
        }
        case SL_DECL_FUNC: {
            ast_printf(out, "(fn %s : %T (", decl->dl_name, decl->dl_type);
            dl_list_print(out, decl->dl_params);
            fprintf(out, ") (");
            ex_list_print(out, decl->dl_body);
            fprintf(out, "))");
            return;
        }
        case SL_DECL_PARAM: {
            ast_printf(out, "(param %s : %T)", decl->dl_name, decl->dl_type);
            return;
        }
    }
    assert(0 && "dl_print missing case");
}


void ex_print(FILE* out, const sl_expr_t* expr)
{
    if (!expr) {
        fprintf(out, "(null)");
        return;
    }
    switch (expr->ex_tag) {
        case SL_EXPR_INT:
            // TODO: actually allow 64bit numbers!
            fprintf(out, "(int %d)", (int)expr->ex_value);
            return;
        case SL_EXPR_BOOL:
            fprintf(out, "(bool %s)", expr->ex_value ? "true" : "false");
            return;
        case SL_EXPR_VOID:
            fprintf(out, "(void)");
            return;
        case SL_EXPR_BINOP:
            ast_printf(out, "(op '%O' %E %E)",
                    expr->ex_op, expr->ex_left, expr->ex_right);
            return;
        case SL_EXPR_LET:
            ast_printf(out, "(let %s : %T %E)", expr->ex_name,
                    expr->ex_type_ann, expr->ex_init);
            return;
        case SL_EXPR_CALL:
            fprintf(out, "(call %s (", expr->ex_fn_name);
            ex_list_print(out, expr->ex_fn_args);
            fprintf(out, "))");
            return;
        case SL_EXPR_NEW:
            fprintf(out, "(new %s (", expr->ex_new_ctor);
            ex_list_print(out, expr->ex_new_args);
            fprintf(out, "))");
            return;
        case SL_EXPR_VAR:
            fprintf(out, "(var %s)", expr->ex_var);
            return;
        case SL_EXPR_RETURN:
            ast_printf(out, "(return %E)", expr->ex_ret_arg);
            return;
        case SL_EXPR_BREAK:
            fprintf(out, "(break)");
            return;
        case SL_EXPR_LOOP:
            ast_printf(out, "(loop (");
            ex_list_print(out, expr->ex_loop_body);
            fprintf(out, "))");
            return;
        case SL_EXPR_DEREF:
            ast_printf(out, "(deref %E)", expr->ex_deref_arg);
            return;
        case SL_EXPR_ADDROF:
            ast_printf(out, "(addrof %E)", expr->ex_addrof_arg);
            return;
        case SL_EXPR_MEMBER:
            ast_printf(out, "(member %E %s)", expr->ex_composite, expr->ex_member);
            return;
        case SL_EXPR_IF:
            ast_printf(out, "(if %E (", expr->ex_if_cond);
            ex_list_print(out, expr->ex_if_cons);
            fprintf(out, ") (");
            ex_list_print(out, expr->ex_if_alt);
            fprintf(out, "))");
            return;
    }
    assert(0 && "ex_print missing case");
}

void ty_print(FILE* out, const sl_type_t* type)
{
    switch (type->ty_tag) {
        case SL_TYPE_NAME:
            fprintf(out, "%s", type->ty_name);
            if (type->ty_size != -1) {
                fprintf(out, ".%lu.%lu", type->ty_size, type->ty_alignment);
            }
            return;
        case SL_TYPE_PTR:
            ast_printf(out, "*%T", type->ty_pointee);
            return;
        case SL_TYPE_ARRAY:
            ast_printf(out, "%T[]", type->ty_pointee);
            return;
        case SL_TYPE_FUNC:
            fprintf(out, "<fn>");
            return;
    }
    assert(0 && "ty_print missing case");
}

void ast_printf(FILE* out, const char* fmt, ...)
{
    const char *cp = fmt;
    char c;

    va_list valist;
    va_start(valist, fmt);

    flockfile(out);
    while ((c = *cp++)) {
        if (c == '%') {
            c = *cp++;
            switch (c) {
                case 'c':
                {
                    int cc = va_arg(valist, int);
                    putc_unlocked(cc, out);
                    break;
                }
                case 'd':
                {
                    /* 43 is maximum possible length of 128-bit integer
                     * string representation */
                    char str[44] = {};
                    char* s = str + sizeof str - 1;
                    int n = va_arg(valist, int);
                    int m = (n < 0) ? -n : n;
                    do {
                        *--s = '0' + m%10;
                        m /= 10;
                    } while (m); // use do while to get printing for 0
                    if (n < 0) {
                        *--s = '-';
                    }
                    do {
                        putc_unlocked(*s++, out);
                    } while (*s);
                    break;
                }
                case 's':
                {
                    const char* cs = va_arg(valist, const char*);
                    while (*cs) { putc_unlocked(*cs++, out); }
                    break;
                }
                case 'D':
                {
                    funlockfile(out); // not sure if re-entrant
                    dl_print(out, va_arg(valist, sl_decl_t*));
                    flockfile(out); // not sure if re-entrant
                    break;
                }
                case 'E':
                {
                    funlockfile(out); // not sure if re-entrant
                    ex_print(out, va_arg(valist, sl_expr_t*));
                    flockfile(out); // not sure if re-entrant
                    break;
                }
                case 'O':
                {
                    /* binary/unary operation */
                    int cc = va_arg(valist, int);
                    if (cc < 255) {
                        putc_unlocked(cc, out);
                    } else {
                        char* op_str = NULL;
                        switch (cc) {
                            case SL_TOK_LOR: op_str = "||"; break;
                            case SL_TOK_LAND: op_str = "&&"; break;
                            case SL_TOK_EQ: op_str = "=="; break;
                            case SL_TOK_NEQ: op_str = "!="; break;
                            case SL_TOK_LE: op_str = "<="; break;
                            case SL_TOK_GE: op_str = ">="; break;
                            case SL_TOK_LSH: op_str = "<<"; break;
                            case SL_TOK_RSH: op_str = ">>"; break;
                            case SL_TOK_SARROW: op_str = "->"; break;
                        }
                        assert(op_str && "missing operator case");
                        const char* cs = op_str;
                        while (*cs) { putc_unlocked(*cs++, out); }
                    }
                    break;
                }
                case 'T':
                {
                    funlockfile(out); // not sure if re-entrant
                    ty_print(out, va_arg(valist, sl_type_t*));
                    flockfile(out); // not sure if re-entrant
                    break;
                }
                case '%':
                    putc_unlocked(c, out);
                    break;
                case '\0': --cp; break;
                default: assert(0 && "need to add another fmt specifier");
            }
        } else {
            putc_unlocked(c, out);
        }
    }
    funlockfile(out);

    va_end(valist);
}

static int dl_num_params(sl_decl_t* decl)
{
    sl_decl_t* param;
    int n = 0;
    for (param = decl->dl_params; param; param = param->dl_list) {
        n += 1;
    }
    return n;
}

int dl_struct_num_fields(sl_decl_t* struct_decl)
{
    assert(struct_decl->dl_tag == SL_DECL_STRUCT);
    return dl_num_params(struct_decl);
}

int dl_func_num_params(sl_decl_t* func_decl)
{
    assert(func_decl->dl_tag == SL_DECL_FUNC);
    return dl_num_params(func_decl);
}

int ty_cmp(sl_type_t* t1, sl_type_t* t2)
{
    assert(t1);
    assert(t2);
    if (t1 == t2) {
        return 0;
    }

    int cmp = t2->ty_tag - t1->ty_tag;
    if (cmp != 0) {
        return cmp;
    }
    switch (t1->ty_tag) {
        case SL_TYPE_NAME:
            return strcmp(t1->ty_name, t2->ty_name);
        case SL_TYPE_PTR:
            return ty_cmp(t1->ty_pointee, t2->ty_pointee);
        case SL_TYPE_ARRAY:
            return ty_cmp(t1->ty_pointee, t2->ty_pointee);
        case SL_TYPE_FUNC:
            return 1; // FIXME
    }
}

/*
 * Defined by the flex lexer
 */
extern FILE* yyin;

/*
 * Tells flex that once it reaches EOF, that there's no new yyin.
 * i.e. it's over.
 */
int yywrap()
{
    return 1;
}


void yyerror(sl_parse_param_t* pp, const char* msg)
{
    fprintf(stderr, "%s:%d: error: %s\n", pp->slpp_filename, yylineno, msg);
    extern const char* yytext;
    fprintf(stderr, "	yytext = %s\n", yytext);
}

/*
 * parse_file is not reentrant
 */
sl_decl_t* parse_file(Arena_T arena, const char* filename)
{
    sl_decl_t* result = NULL;
    ast_arena = arena;
    sl_parse_param_t parse_param = {
        .slpp_filename = NULL,
        .slpp_arena = arena,
        .slpp_root = NULL,
    };

    FILE* old_yyin = yyin;

    if (strcmp(filename, "-") == 0) {
        yyin = stdin; // default anyway
        parse_param.slpp_filename = "<stdin>";
    } else {
        yyin = fopen(filename, "r");
        if (!yyin) {
            perror(filename);
            goto cleanup;
        }
        parse_param.slpp_filename = filename;
    }


    if (yyparse(&parse_param) == 0) {
        result = parse_param.slpp_root;
    }

cleanup:
    // Restore state
    ast_arena = NULL; // This will break shit
    if (yyin != NULL && yyin != stdin) {
        fclose(yyin);
    }
    yyin = old_yyin;

    return result;
}
