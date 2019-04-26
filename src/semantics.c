#include <stdlib.h> // malloc, free, abort
#include <assert.h> // assert
#include <string.h> // strcmp
#include "semantics.h"
#include "colours.h"

// type infering declarations - requires -std=gnu11
#define VAR(_name, _expression) typeof(_expression) _name = (_expression)

// used to define check types of our macros arguments
static void check_int(int i) {}
static void check_psem_info_t(sem_info_t* sem_info) {}

#define elprintf(fmt, sem_info, lineno, ...) do { \
    check_psem_info_t(sem_info); check_int(lineno); \
    ast_printf(stderr, "%s:%d: %serror:%s " fmt "\n", \
            (sem_info)->si_filename, lineno, \
            term_colours.red, term_colours.clear, ##__VA_ARGS__); \
} while (0)

static scope_t* scope_new()
{
    scope_t* s = malloc(sizeof *s);
    if (!s) { perror("out of memory"); abort(); }
    s->sc_bindings = Table_new(0, NULL, NULL);
    s->sc_parent = NULL;
    return s;
}

static void scope_free(scope_t** scope)
{
    Table_free(&(*scope)->sc_bindings);
    assert(scope && *scope);
    free(*scope);
    *scope = NULL;
}

static void push_scope(sem_info_t* info)
{
    scope_t* new_scope = scope_new();
    new_scope->sc_parent = info->si_current_scope;
    info->si_current_scope = new_scope;
}

static void pop_scope(sem_info_t* info)
{
    scope_t* old_scope = info->si_current_scope;
    assert(old_scope);
    info->si_current_scope = old_scope->sc_parent;
    scope_free(&old_scope);
}

static int declare_in_current_scope(
        sem_info_t* info, sl_sym_t name, sl_type_t* type)
{
    if (Table_put(info->si_current_scope->sc_bindings, name, type) != NULL) {
        return -1;
    }
    return 0;
}

static sl_type_t* lookup_var(sem_info_t* info, sl_sym_t name)
{
    scope_t* scope = info->si_current_scope;
    for (; scope; scope = scope->sc_parent) {
        sl_type_t* found = Table_get(scope->sc_bindings, name);
        if (found) {
            return found;
        }
    }
    return NULL;
}

static sl_decl_t* lookup_decl(sem_info_t* sem_info, sl_sym_t name, int tag)
{
    sl_decl_t* x;
    for (x = sem_info->si_program; x; x = x->dl_list) {
        if (x->dl_tag == tag) {
            if (x->dl_name == name) {
                return x;
            }
        }
    }
    return NULL;
}

static sl_decl_t* lookup_struct_decl(sem_info_t* sem_info, sl_sym_t type_name)
{
    return lookup_decl(sem_info, type_name, SL_DECL_STRUCT);
}

static _Bool type_exists(sem_info_t* sem_info, sl_type_t* type)
{
    // extract type name only, e.g. **int[] -> *int[] -> int[] -> int
    sl_type_t* t = type;
    while (t->ty_tag != SL_TYPE_NAME) {
        assert(t->ty_tag == SL_TYPE_PTR || t->ty_tag == SL_TYPE_ARRAY);
        t = t->ty_pointee;
    }
    // check for builtins
    if (t->ty_name == symbol("int") || t->ty_name == symbol("void")) {
        return 1;
    }
    return (lookup_struct_decl(sem_info, t->ty_name) != NULL);
}

static sl_decl_t* lookup_func_decl(sem_info_t* sem_info, sl_sym_t fn_name)
{
    return lookup_decl(sem_info, fn_name, SL_DECL_FUNC);
}

static int verify_expr(sem_info_t* info, sl_expr_t* e);


static int verify_expr_binop(sem_info_t* info, sl_expr_t* expr)
{
    int result = 0;
    switch (expr->ex_op) {
        case '+':
        case '-':
        case '*':
        case '/':
            result += verify_expr(info, expr->ex_left);
            result += verify_expr(info, expr->ex_right);

            VAR(t1, expr->ex_left->ex_type);
            VAR(t2, expr->ex_right->ex_type);
            VAR(int_type, info->si_builtin_types.int_type);
            if (ty_cmp(t1, int_type) != 0) {
                elprintf("operands of binary operation '%c' should be of "
                        "type 'int'", info, expr->ex_left->ex_line,
                        expr->ex_op);
                result -= 1;
            }
            if (ty_cmp(t2, int_type) != 0) {
                elprintf("operands of binary operation '%c' should be of "
                        "type 'int'", info, expr->ex_right->ex_line,
                        expr->ex_op);
                result -= 1;
            }

            expr->ex_type = info->si_builtin_types.int_type;
            break;
        default:
            fprintf(stderr, "expr->ex_op = 0x%x (%c)\n",
                    expr->ex_op, expr->ex_op);
            assert(0 && "unexpected binary op");
    }
    return result;
}

static int verify_expr_let(sem_info_t* info, sl_expr_t* expr)
{
    int result = 0;
    result += verify_expr(info, expr->ex_init);
    // check type exists
    if (!type_exists(info, expr->ex_type_ann)) {
        elprintf("unknown type: '%T'", info, expr->ex_line, expr->ex_type_ann);
        result -= 1;
    }

    if (ty_cmp(expr->ex_type_ann, expr->ex_init->ex_type) != 0) {
        elprintf("type of expression initialising '%s', '%T', does not "
                "match the declared type: '%T'", info,
                expr->ex_line, expr->ex_name, expr->ex_init->ex_type,
                expr->ex_type_ann);
        result -= 1;
    }

    // add name to scope
    if (declare_in_current_scope(info, expr->ex_name, expr->ex_type_ann) < 0) {
        elprintf("name '%s' already defined in this scope", info,
                expr->ex_line, expr->ex_name);
        result -= 1;
    }

    // what is the type of a let expression? void, or the type of the
    // binding? Or void?
    expr->ex_type = info->si_builtin_types.void_type; // ?

    return result;
}

static int verify_expr_call(sem_info_t* info, sl_expr_t* expr)
{
    // check the argument expressions
    // check that the function has been defined
    // check that the arguments have the same count and types as the parameters
    // (or can be promoted?)
    // type the node with the same type as the return type of the function

    int result = 0;
    int num_args = 0;
    for (VAR(arg, expr->ex_fn_args); arg; arg = arg->ex_list) {
        result += verify_expr(info, arg);
        num_args += 1;
    }

    sl_decl_t* fn_decl = lookup_func_decl(info, expr->ex_fn_name);
    if (fn_decl == NULL) {
        elprintf("call to undefined function '%s'",
                info, expr->ex_line, expr->ex_fn_name);
        // XXX unknown type
        expr->ex_type = info->si_builtin_types.void_type;
        return result - 1;
    }

    expr->ex_type = fn_decl->dl_type;

    int num_params = 0;
    for (VAR(param, fn_decl->dl_params); param; param = param->dl_list) {
        num_params += 1;
    }
    if (num_args < num_params) {
        elprintf("not enough arguments passed to function '%s', expected %d",
                info, expr->ex_line, expr->ex_fn_name, num_params);
        result -= 1;
    } else if (num_args > num_params) {
        elprintf("too many arguments passed to function '%s', expected %d",
                info, expr->ex_line, expr->ex_fn_name, num_params);
        result -= 1;
    }

    VAR(arg, expr->ex_fn_args);
    VAR(param, fn_decl->dl_params);
    for (int i = 1; arg && param; arg = arg->ex_list, param = param->dl_list, i++) {
        if (ty_cmp(arg->ex_type, param->dl_type) != 0) {
            elprintf("argument %d passed to '%s' has type '%T' but expected '%T'",
                    info, expr->ex_line,
                    i, expr->ex_fn_name, arg->ex_type, param->dl_type);
            result -= 1;
        }
    }
    return result;
}

static int verify_expr_new(sem_info_t* info, sl_expr_t* expr)
{
    // should basically be the same as verifying a function call.
    // except, can we have named parameters?

    int result = 0;
    int num_args = 0;
    for (VAR(arg, expr->ex_new_args); arg; arg = arg->ex_list) {
        result += verify_expr(info, arg);
        num_args += 1;
    }

    sl_decl_t* struct_decl = lookup_struct_decl(info, expr->ex_new_ctor);
    if (struct_decl == NULL) {
        elprintf("attempting to allocate undeclared struct '%s'",
                info, expr->ex_line, expr->ex_new_ctor);
        // XXX unknown type
        expr->ex_type = info->si_builtin_types.void_type;
        return result - 1;
    }

    expr->ex_type = ty_type_pointer(ty_type_name(struct_decl->dl_name));

    int num_fields = dl_struct_num_fields(struct_decl);
    if (num_args < num_fields) {
        elprintf("not enough fields passed to constructor for struct '%s', expected %d",
                info, expr->ex_line, expr->ex_new_ctor, num_fields);
        result -= 1;
    } else if (num_args > num_fields) {
        elprintf("too many fields passed to constructor for struct '%s', expected %d",
                info, expr->ex_line, expr->ex_new_ctor, num_fields);
        result -= 1;
    }

    VAR(arg, expr->ex_new_args);
    VAR(param, struct_decl->dl_params);
    for (int i = 1; arg && param; arg = arg->ex_list, param = param->dl_list, i++) {
        if (ty_cmp(arg->ex_type, param->dl_type) != 0) {
            elprintf("field %d in '%s' constructor has type '%T' but expected '%T'",
                    info, expr->ex_line,
                    i, expr->ex_new_ctor, arg->ex_type, param->dl_type);
            result -= 1;
        }
    }
    return result;
}

static int verify_expr_var(sem_info_t* info, sl_expr_t* expr)
{
    // lookup the variable
    // type the node the same as the variable was declared

    sl_type_t* var_type = lookup_var(info, expr->ex_var);
    if (var_type == NULL) {
        elprintf("use of undeclared identifier '%s'", info, expr->ex_line,
                expr->ex_var);
        // XXX unknown type
        expr->ex_type = info->si_builtin_types.void_type;
        return -1;
    }
    expr->ex_type = var_type;
    return 0;
}

static int verify_expr_return(sem_info_t* info, sl_expr_t* expr)
{
    // check the return expression
    // check the return expression type matches the function return type
    // type node as void
    int result = 0;
    VAR(fn_ret_type, info->si_current_fn->dl_type);
    if (expr->ex_ret_arg) {
        result += verify_expr(info, expr->ex_ret_arg);
        // How do I get the current function
        if (ty_cmp(expr->ex_ret_arg->ex_type, fn_ret_type) != 0) {
            elprintf("return value with incorrect type '%T', function '%s' "
                    "has return type '%T'", info, expr->ex_ret_arg->ex_line,
                    expr->ex_ret_arg->ex_type, info->si_current_fn->dl_name,
                    info->si_current_fn->dl_type);
            result -= 1;
        }
    } else {
        if (ty_cmp(info->si_builtin_types.void_type, fn_ret_type) != 0) {
            elprintf("missing return value for non-void function '%s'",
                    info, expr->ex_line,
                    info->si_current_fn->dl_name);
            result -= 1;
        }
    }
    expr->ex_type = info->si_builtin_types.void_type;
    return result;
}

static int verify_expr_break(sem_info_t* info, sl_expr_t* expr)
{
    // check we are in a loop
    int result = 0;

    if (info->si_loop_depth == 0) {
        elprintf("occurence of break outside a loop", info, expr->ex_line);
        result -= 1;
    }

    expr->ex_type = info->si_builtin_types.void_type;

    return result;
}

static int verify_expr_loop(sem_info_t* info, sl_expr_t* expr)
{
    int result = 0;
    info->si_loop_depth += 1;
    push_scope(info);

    for (VAR(stmt, expr->ex_loop_body); stmt; stmt = stmt->ex_list) {
        result += verify_expr(info, stmt);
    }

    // What is the type of a loop... well, if the loop is the last expr in a
    // function, is it the type of the function?
    // It's the bottom type. The type that inhabits all types
    // this will do for the time being
    expr->ex_type = info->si_builtin_types.void_type;

    pop_scope(info);
    info->si_loop_depth -= 1;
    return result;
}

static int verify_expr_deref(sem_info_t* info, sl_expr_t* expr)
{
    // check that the sub-expression being dereferenced has a pointer type
    int result = 0;

    result += verify_expr(info, expr->ex_deref_arg);

    if (expr->ex_deref_arg->ex_type->ty_tag != SL_TYPE_PTR)
    {
        elprintf("cannot dereference non-pointer type '%T'",
                info, expr->ex_line, expr->ex_deref_arg->ex_type);
        // XXX unknown type
        expr->ex_type = info->si_builtin_types.void_type;
        return result - 1;
    }

    expr->ex_type = expr->ex_deref_arg->ex_type->ty_pointee;

    return result;
}

static int verify_expr(sem_info_t* info, sl_expr_t* expr)
{
    switch (expr->ex_tag) {
        case SL_EXPR_INT:
            expr->ex_type = info->si_builtin_types.int_type;
            return 0;
        case SL_EXPR_BINOP:
            return verify_expr_binop(info, expr);
        case SL_EXPR_LET:
            return verify_expr_let(info, expr);
        case SL_EXPR_CALL:
            return verify_expr_call(info, expr);
        case SL_EXPR_NEW:
            return verify_expr_new(info, expr);
        case SL_EXPR_VAR:
            return verify_expr_var(info, expr);
        case SL_EXPR_RETURN:
            return verify_expr_return(info, expr);
        case SL_EXPR_BREAK:
            return verify_expr_break(info, expr);
        case SL_EXPR_LOOP:
            return verify_expr_loop(info, expr);
        case SL_EXPR_DEREF:
            return verify_expr_deref(info, expr);
    }
    assert(0 && "verify_expr missing case");
}

static int verify_decl_func(sem_info_t* sem_info, sl_decl_t* decl)
{
    int result = 0;
    push_scope(sem_info);

    // check parameters, types known, no duplicates
    for (sl_decl_t* param = decl->dl_params; param;
            param = param->dl_list) {
        // check type is known
        if (!type_exists(sem_info, param->dl_type)) {
            elprintf("unknown type: '%T'", sem_info, param->dl_line,
                    param->dl_type);
            result -= 1;
        }

        if (declare_in_current_scope(sem_info, param->dl_name, param->dl_type) < 0) {
            elprintf("second declaration of parameter '%s' in function '%s'",
                    sem_info, param->dl_line, param->dl_name, decl->dl_name);
            result -= 1;
        }
    }

    push_scope(sem_info);
    sem_info->si_current_fn = decl;

    for (sl_expr_t* e = decl->dl_body; e; e = e->ex_list) {
        result += verify_expr(sem_info, e);
    }

    // if non-void, check that last expression is either a return, or
    // the same type as the function return type, or a loop
    sl_expr_t* final_expr = decl->dl_body;
    while (final_expr->ex_list) {
        final_expr = final_expr->ex_list;
    }

    if (final_expr) {
        if (final_expr->ex_tag == SL_EXPR_RETURN) {
            // OK.. probably
        } else if (final_expr->ex_tag == SL_EXPR_LOOP) {
            // OK
        } else if (ty_cmp(final_expr->ex_type, decl->dl_type) != 0) {
            elprintf("final expression in '%s' has type '%T' but expected '%T'",
                    sem_info, final_expr->ex_line,
                    decl->dl_name, final_expr->ex_type, decl->dl_type);
            result -= 1;
        }
    } else {
        // No expressions. function must return void
        if (ty_cmp(decl->dl_type, sem_info->si_builtin_types.void_type) != 0) {
            elprintf("no expressions in non-void function '%s'",
                    sem_info, decl->dl_line);
            result -= 1;
        }
    }

    sem_info->si_current_fn = NULL;
    pop_scope(sem_info);
    pop_scope(sem_info);
    return result;
}

static int verify_decl_struct(sem_info_t* sem_info, sl_decl_t* decl)
{
    int result = 0;
    for (sl_decl_t* field = decl->dl_params; field;
            field = field->dl_list) {
        // check type is known
        if (!type_exists(sem_info, field->dl_type)) {
            elprintf("unknown type: '%T'", sem_info, field->dl_line,
                    field->dl_type);
            result -= 1;
        }

        // check type is not recursive
        VAR(field_type, field->dl_type);
        if (field_type->ty_tag == SL_TYPE_NAME
                && field_type->ty_name == decl->dl_name) {
            elprintf("recursive type not allowed, use pointer instead",
                    sem_info, field->dl_line);
            result -= 1;
        }

        // check names aren't used twice
        for (sl_decl_t* field2 = field->dl_list; field2;
                field2 = field2->dl_list) {
            if (field->dl_name == field2->dl_name) {
                elprintf("second declaration of field '%s' in struct '%s'",
                        sem_info, field2->dl_line, field->dl_name, decl->dl_name);
                result -= 1;
            }
        }
    }
    return result;
}

static int verify_decl(sem_info_t* sem_info, sl_decl_t* decl)
{
    int result = 0;
    switch (decl->dl_tag) {
        case SL_DECL_STRUCT:
            return verify_decl_struct(sem_info, decl);
        case SL_DECL_FUNC:
            return verify_decl_func(sem_info, decl);
        case SL_DECL_PARAM:
            assert(0 && "param at top-level ...");
            return result;
    }
    assert(0 && "verify_decl missing case");
}

int sem_verify_and_type_program(const char* filename, sl_decl_t* program)
{
    int result = 0;
    sem_info_t sem_info = {
        .si_program = program,
        .si_filename = (strcmp(filename, "-") == 0) ? "<stdin>" : filename,
        .si_root_scope = scope_new(),
    };
    sem_info.si_current_scope = sem_info.si_root_scope;

    // Add builtin types to sem_info
    sem_info.si_builtin_types.int_type = ty_type_name(symbol("int"));
    sem_info.si_builtin_types.void_type = ty_type_name(symbol("void"));

    // Add names to root scope
    for (sl_decl_t* decl = program; decl; decl = decl->dl_list) {

        if (decl->dl_tag == SL_DECL_STRUCT) {
            for (sl_decl_t* decl2 = program; decl2 != decl; decl2 = decl2->dl_list) {
                elprintf("second declaration of type '%s' in module",
                        &sem_info, decl2->dl_line, decl2->dl_name);
                result -= 1;
            }
        } else if (decl->dl_tag == SL_DECL_FUNC) {
            // we actually shouldn't be declaring types in the root scope
            if (declare_in_current_scope(&sem_info, decl->dl_name, ty_type_func()) < 0) {
                elprintf("second declaration of function '%s' in module",
                        &sem_info, decl->dl_line, decl->dl_name);
                result -= 1;
            }
        } else {
            assert(0 && "unexpected decl type at top level");
        }
    }

    if (result < 0) {
        goto cleanup;
    }

    for (sl_decl_t* decl = program; decl; decl = decl->dl_list) {
        result += verify_decl(&sem_info, decl);
    }

cleanup:
    scope_free(&sem_info.si_root_scope);
    return result;
}
