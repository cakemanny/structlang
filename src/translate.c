#include "translate.h"
#include <assert.h> /* assert */
#include <stdbool.h> /* bool */
#include <string.h>
#include "mem.h" // xmalloc
#include "grammar.tab.h"
#include "arena_util.h"

#define EX_LIST_IT(it, head) sl_expr_t* it = (head); it; it = it->ex_list
#define var __auto_type
#define Alloc(arena, size) Arena_alloc(arena, size, __FILE__, __LINE__)

typedef struct translate_info_t {
    const sl_decl_t* program;
    temp_state_t* temp_state;
    sl_sym_t current_loop_end;
    sl_sym_t function_end_label;
    bool is_end_label_used;
    sl_fragment_t* string_fragments;
    Arena_T ret_arena;
    Arena_T scratch;
} translate_info_t;

struct translate_exp_t;
typedef struct translate_exp_t translate_exp_t;

typedef struct label_bifunc_t {
    tree_stm_t* (*lbf_fn)(sl_sym_t, sl_sym_t, void* cl, Arena_T);
    void* lbf_cl;
} label_bifunc_t;

struct translate_exp_t {
    enum {
        TR_EXP_EX = 1,
        TR_EXP_NX,
        TR_EXP_CX
    } tr_exp_tag;
    union {
        tree_exp_t* tr_exp_ex;
        tree_stm_t* tr_exp_nx;
        label_bifunc_t tr_exp_cx;
    };
};

/*
 * should probably come from the target or...
 */
static const unsigned bool_size = 1;


static int round_up_size(int size, int multiple) __attribute__((const));
static int round_up_size(int size, int multiple)
{
    return ((size + multiple - 1) / multiple) * multiple;
}

/* Just a helper function, does not allocate */
static label_bifunc_t label_bifunc(
        tree_stm_t* (*fn)(sl_sym_t, sl_sym_t, void* cl, Arena_T),
        void* cl)
{
    label_bifunc_t bifunc = { .lbf_fn = fn, .lbf_cl = cl };
    return bifunc;
}

static tree_stm_t* lbf_call(label_bifunc_t lbf, sl_sym_t t, sl_sym_t f, Arena_T a)
{
    return lbf.lbf_fn(t, f, lbf.lbf_cl, a);
}

static translate_exp_t* translate_ex(tree_exp_t* e)
{
    translate_exp_t* result = xmalloc(sizeof *result);
    result->tr_exp_tag = TR_EXP_EX;
    result->tr_exp_ex = e;
    return result;
}

static translate_exp_t* translate_nx(tree_stm_t* s)
{
    translate_exp_t* result = xmalloc(sizeof *result);
    result->tr_exp_tag = TR_EXP_NX;
    result->tr_exp_nx = s;
    return result;
}

static translate_exp_t* translate_cx(label_bifunc_t genstm)
{
    translate_exp_t* result = xmalloc(sizeof *result);
    result->tr_exp_tag = TR_EXP_CX;
    result->tr_exp_cx = genstm;
    return result;
}

static tree_exp_t* translate_un_ex(translate_info_t* info, translate_exp_t* ex)
{
    temp_state_t* ts = info->temp_state;
    Arena_T ar = info->ret_arena;
    tree_exp_t* ret;
    switch (ex->tr_exp_tag) {
        case TR_EXP_EX:
            ret = ex->tr_exp_ex;
            break;
        case TR_EXP_NX:
            ret = tree_exp_eseq(ex->tr_exp_nx,
                    tree_exp_const(0, ac_word_size, tree_typ_void(ar), ar), ar);
            break;
        case TR_EXP_CX:
        {
            temp_t r = temp_newtemp(ts, bool_size, TEMP_DISP_NOT_PTR);
            sl_sym_t t = temp_newlabel(ts);
            sl_sym_t f = temp_newlabel(ts);

            ret = tree_exp_eseq(
                    tree_stm_seq(
                    tree_stm_seq(
                    tree_stm_seq(
                    tree_stm_seq(
                        tree_stm_move(
                            tree_exp_temp(r, bool_size, tree_typ_bool(ar), ar),
                            tree_exp_const(1, bool_size, tree_typ_bool(ar), ar), ar)
                        ,
                        lbf_call(ex->tr_exp_cx, t, f, ar), ar /* genstm */
                        ),
                        tree_stm_label(f, ar), ar
                        ),
                        tree_stm_move(
                            tree_exp_temp(r, bool_size, tree_typ_bool(ar), ar),
                            tree_exp_const(0, bool_size, tree_typ_bool(ar), ar), ar), ar
                        ),
                        tree_stm_label(t, ar), ar
                        ),
                        tree_exp_temp(r, bool_size, tree_typ_bool(ar), ar), ar
                        );
            break;
        }
    }
    free(ex);
    return ret;
}

static tree_stm_t* translate_un_nx(translate_info_t* info, translate_exp_t* ex)
{
    temp_state_t* ts = info->temp_state;
    Arena_T arena = info->ret_arena;
    tree_stm_t* ret;
    switch (ex->tr_exp_tag) {
        case TR_EXP_EX:
            ret = tree_stm_exp(ex->tr_exp_ex, arena);
            break;
        case TR_EXP_NX:
            ret = ex->tr_exp_nx;
            break;
        case TR_EXP_CX:
        {
            // Just evaluate the conditional and continue...
            // Not sure if it's legal to end with a label...
            // maybe we need a post-label no-op?
            sl_sym_t dst = temp_newlabel(ts);
            ret = tree_stm_seq(
                lbf_call(ex->tr_exp_cx, dst, dst, arena), /* genstm */
                tree_stm_label(dst, arena),
                arena
            );
            break;
        }
    }
    free(ex);
    return ret;
}

static tree_stm_t* unconditional_jump(sl_sym_t dst, Arena_T a)
{
    sl_sym_t* labels = Alloc(a, 1 * sizeof *labels);
    labels[0] = dst;
    return tree_stm_jump(tree_exp_name(dst, a), 1, labels, a);
}

static tree_stm_t* always_true(sl_sym_t t, sl_sym_t f, void* cl, Arena_T a)
{
    return unconditional_jump(t, a);
}

static tree_stm_t* always_false(sl_sym_t t, sl_sym_t f, void* cl, Arena_T a)
{
    return unconditional_jump(f, a);
}

static tree_stm_t* jump_not_zero(sl_sym_t t, sl_sym_t f, void* cl, Arena_T a)
{
    var rhs = (tree_exp_t*)cl;
    return tree_stm_cjump(
            TREE_RELOP_NE, tree_exp_const(0, rhs->te_size, rhs->te_type, a), rhs,
            t, f, a);
}

static label_bifunc_t translate_un_cx(translate_exp_t* ex)
{
    // Treat const 1 and const 0 specially he said
    switch (ex->tr_exp_tag) {
        case TR_EXP_EX:
        {
            tree_exp_t* e = ex->tr_exp_ex;
            free(ex);
            switch (e->te_tag) {
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
        {
            var cx = ex->tr_exp_cx;
            free(ex);
            return cx;
        }
    }
}


static const sl_decl_t* lookup_struct_decl(const sl_decl_t* program, sl_sym_t name)
{
    const sl_decl_t* x;
    for (x = program; x; x = x->dl_list) {
        if (x->dl_tag == SL_DECL_STRUCT) {
            if (x->dl_name == name) {
                return x;
            }
        }
    }
    fprintf(stderr, "type->name = %s\n", name);
    assert(!"unknown type name");
}

struct translated_type {
    sl_sym_t name;
    tree_typ_t* type;
    const struct translated_type *link;
};

static tree_typ_t* find_translated(
        sl_sym_t type_name, const struct translated_type *translated)
{
    for (var t = translated; t; t = t->link) {
        if (t->name == type_name) {
            return t->type;
        }
    }
    return NULL;
}

static tree_typ_t*
translate_type0(
        Arena_T arena, const sl_decl_t* program,
        const sl_type_t* type, const struct translated_type *translated)
{
    switch (type->ty_tag) {
        case SL_TYPE_NAME:
            if (type->ty_name == symbol("int")) {
                return tree_typ_int(arena);
            }
            if (type->ty_name == symbol("bool")) {
                return tree_typ_bool(arena);
            }
            if (type->ty_name == symbol("void")) {
                return tree_typ_void(arena);
            }

            /*
             * To handle recursive definitions, we first allocate the result
             * and then pass it along when translating the fields in case it's
             * there
             */
            tree_typ_t* found = find_translated(type->ty_name, translated);
            if (found) {
                return found;
            }
            tree_typ_t* result = tree_typ_struct(NULL, arena);
            const struct translated_type already_translated = {
                .name = type->ty_name,
                .type = result,
                .link = translated,
            };

            const sl_decl_t* decl = lookup_struct_decl(program, type->ty_name);
            assert(decl->dl_tag == SL_DECL_STRUCT);

            tree_typ_t* fields = NULL;
            for (const sl_decl_t* field = decl->dl_params; field;
                    field = field->dl_list) {
                var translated_field = translate_type0(arena,
                        program, field->dl_type, &already_translated);
                fields = tree_typ_append(fields, translated_field);
            }

            result->tt_fields = fields;
            return result;
        case SL_TYPE_PTR:
            return tree_typ_ptr(
                    translate_type0(arena, program, type->ty_pointee, translated),
                    arena);
        case SL_TYPE_ARRAY:
        case SL_TYPE_FUNC:
            assert(!"not implemented");
    }
}

tree_typ_t*
translate_type(
        Arena_T arena, const sl_decl_t* program, const sl_type_t* type)
{
    return translate_type0(arena, program, type, NULL);
}


/* forward declaration so we can be recursive  */
static translate_exp_t* translate_expr(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr);

static tree_exp_t* translate_var_mem_ref_expr(
        translate_info_t* info, ac_frame_t* frame, int var_id, sl_type_t* type)
{
    Arena_T arena = info->ret_arena;
    struct ac_frame_var* frame_var = NULL;
    for (frame_var = frame->ac_frame_vars; frame_var;
            frame_var = frame_var->acf_list) {
        if (var_id == frame_var->acf_var_id) {
            break;
        }
    }
    assert(frame_var);

    if (frame_var->acf_tag == ACF_ACCESS_REG) {
        return tree_exp_temp(frame_var->acf_reg, frame_var->acf_size,
                translate_type(info->ret_arena, info->program, type), arena);
    }
    assert(frame_var->acf_tag == ACF_ACCESS_FRAME);

    tree_exp_t* result = tree_exp_mem(
        (frame_var->acf_offset == 0)
        ? tree_exp_temp(
            frame->acf_target->tgt_fp, ac_word_size,
            tree_typ_ptr(tree_typ_void(arena), arena), arena)
        : tree_exp_binop(
            TREE_BINOP_PLUS,
            tree_exp_temp(
                frame->acf_target->tgt_fp, ac_word_size,
                tree_typ_ptr(tree_typ_void(arena), arena), arena),
            tree_exp_const(
                frame_var->acf_offset, ac_word_size, tree_typ_ptr_diff(arena),
                arena),
            arena
        ),
        frame_var->acf_size,
        translate_type(info->ret_arena, info->program, type),
        arena
    );
    return result;
}

static translate_exp_t* translate_expr_var(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    tree_exp_t* result = translate_var_mem_ref_expr(info, frame,
            expr->ex_var_id, expr->ex_type);
    return translate_ex(result);
}

static translate_exp_t* translate_expr_int(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    var ar = info->ret_arena;
    tree_exp_t* result = tree_exp_const(
            expr->ex_value,
            size_of_type(info->program, expr->ex_type),
            tree_typ_int(ar), ar);
    return translate_ex(result);
}

static translate_exp_t* translate_expr_bool(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    var ar = info->ret_arena;
    tree_exp_t* result = tree_exp_const(
            expr->ex_value,
            size_of_type(info->program, expr->ex_type),
            tree_typ_bool(ar), ar);
    return translate_ex(result);
}

static translate_exp_t* translate_expr_void(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    var ar = info->ret_arena;
    /* TODO: or shoud this have 0 size? */
    return translate_ex(
            tree_exp_const(0, ac_word_size, tree_typ_void(ar), ar));
}


struct logical_and_or_cl {
    temp_state_t* ts;
    tree_exp_t* lhe;
    tree_exp_t* rhe;
};

static tree_stm_t* logical_or(sl_sym_t t, sl_sym_t f, void* cl, Arena_T a) {
    struct logical_and_or_cl* uncl = (struct logical_and_or_cl*) cl;
    var z = temp_newlabel(uncl->ts);
    var result = jump_not_zero(t, z, uncl->lhe, a);
    result = tree_stm_seq(result, tree_stm_label(z, a), a);
    result = tree_stm_seq(result, jump_not_zero(t, f, uncl->rhe, a), a);
    return result;
}

static tree_stm_t* logical_and(sl_sym_t t, sl_sym_t f, void* cl, Arena_T a) {
    struct logical_and_or_cl* uncl = (struct logical_and_or_cl*) cl;
    var z = temp_newlabel(uncl->ts);
    var result = jump_not_zero(z, f, uncl->lhe, a);
    result = tree_stm_seq(result, tree_stm_label(z, a), a);
    result = tree_stm_seq(result, jump_not_zero(t, f, uncl->rhe, a), a);
    return result;
}


// closure for the compare_and_jump function
struct compare_and_jump_cl {
    int relop;
    tree_exp_t* lhe;
    tree_exp_t* rhe;
};

static tree_stm_t* compare_and_jump(sl_sym_t t, sl_sym_t f, void* cl, Arena_T a)
{
    struct compare_and_jump_cl *uncl = (struct compare_and_jump_cl*) cl;
    return tree_stm_cjump(uncl->relop, uncl->lhe, uncl->rhe, t, f, a);
}


static translate_exp_t* translate_expr_binop(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    translate_exp_t* lhs = translate_expr(info, frame, expr->ex_left);
    translate_exp_t* rhs = translate_expr(info, frame, expr->ex_right);

    tree_exp_t* lhe = translate_un_ex(info, lhs); lhs = NULL;
    tree_exp_t* rhe = translate_un_ex(info, rhs); rhs = NULL;

    // break out of here and compose some branches
    switch (expr->ex_op) {
        // I think here, we actually want CX nodes. So maybe the unEx is a
        // bit premature
        case SL_TOK_LOR:
            {
                // a || b:
                // t, f ->
                //      CJUMP(!=, a, 0, t, z)
                //  label z:
                //      CJUMP(!=, b, 0, t, f)
                struct logical_and_or_cl* cl = Alloc(info->scratch, sizeof *cl);
                cl->ts = info->temp_state;
                cl->lhe = lhe;
                cl->rhe = rhe;
                return translate_cx(label_bifunc(logical_or, cl));
            }
        case SL_TOK_LAND:
            {
                // a && b:
                // t, f ->
                //      CJUMP(!=, a, 0, z, f)
                //  label z:
                //      CJUMP(!=, b, 0, t, f)
                struct logical_and_or_cl* cl = Alloc(info->scratch, sizeof *cl);
                cl->ts = info->temp_state;
                cl->lhe = lhe;
                cl->rhe = rhe;
                return translate_cx(label_bifunc(logical_and, cl));
            }
        default: break;
    }

    if (expr->ex_op == SL_TOK_EQ || expr->ex_op == SL_TOK_NEQ) {
        // if the types of the things being compared is > 1 word, then we
        // need to rewrite it, as an AND of comparisons
        // might need to do this before the subexpression conversion
        // solve using a rewrite layer
        //
        // ... later dan: I think we've done that ^
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
        struct compare_and_jump_cl* cl = Alloc(info->scratch, sizeof *cl);
        cl->relop = relop;
        cl->lhe = lhe;
        cl->rhe = rhe;
        return translate_cx(label_bifunc(compare_and_jump, cl));
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

    tree_exp_t* result = tree_exp_binop(op, lhe, rhe, info->ret_arena);
    return translate_ex(result);
}

static translate_exp_t* translate_expr_let(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    assert(size_of_type(info->program, expr->ex_type) <= ac_word_size
            && "TODO: larger sizes");

    // basically, this is an assignment
    // translate the init expr as the right hand side
    translate_exp_t* rhs = translate_expr(info, frame, expr->ex_init);
    tree_exp_t* rhe = translate_un_ex(info, rhs); rhs = NULL;


    // MAYBE we need a SIZE for our MOVE instruction?
    // DOES this WORK with structs??

    // the lhs is a memory expression giving the location of the local in the
    // stack frame
    //
    tree_exp_t* dst =
        translate_var_mem_ref_expr(info, frame, expr->ex_let_id, expr->ex_type);

    tree_stm_t* result = tree_stm_move(dst, rhe, info->ret_arena);

    return translate_nx(result);
}

static sl_sym_t label_for_descriptor(
        translate_info_t* info, char* descriptor) {

    for (var frag = info->string_fragments; frag; frag = frag->fr_list) {
        if (strcmp(frag->fr_string, descriptor) == 0) {
            return frag->fr_label;
        }
    }

    sl_sym_t descriptor_label = temp_newlabel(info->temp_state);

    // move from scratch
    descriptor = strdup_arena(info->ret_arena, descriptor);

    info->string_fragments =
        fr_append(info->string_fragments,
                sl_string_fragment(
                    descriptor_label, descriptor, info->ret_arena));
    return descriptor_label;
}

static translate_exp_t* translate_expr_new(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    // 1. allocate some memory, assigning the locations to a temp r
    // 2. evaluate each param and assign it to the correct offset from r
    var ar = info->ret_arena;

    temp_t r = temp_newtemp(info->temp_state, ac_word_size, TEMP_DISP_PTR);

    sl_type_t* struct_type = expr->ex_type->ty_pointee;

    char* descriptor = ac_record_descriptor_for_type(
            info->scratch, info->program, struct_type);

    var arg_exp = tree_exp_name(label_for_descriptor(info, descriptor),ar);
    arg_exp->te_size = 8; // FIXME: pass in the target

    tree_stm_t* assign = tree_stm_move(
            tree_exp_temp(
                r, r.temp_size,
                translate_type(ar, info->program, expr->ex_type), ar),
            tree_exp_call(
                tree_exp_name("sl_alloc_des", ar),
                arg_exp,
                ac_word_size,
                translate_type(ar, info->program, expr->ex_type),
                ac_calculate_ptr_maps(frame, expr->ex_new_defd_vars, ar),
                ar
            ),
            ar
    );

    // FIXME: this is not necessary
    tree_stm_t* init_seq = tree_stm_seq(assign, NULL, ar);
    tree_stm_t** init_seq_tail = &init_seq;

    int offset = 0;
    for (var arg = expr->ex_new_args; arg; arg = arg->ex_list) {
        var init_exp = translate_expr(info, frame, arg);
        size_t arg_size = size_of_type(info->program, arg->ex_type);
        var arg_alignment = alignment_of_type(info->program, arg->ex_type);

        offset = round_up_size(offset, arg_alignment);

        tree_stm_t* init = tree_stm_move(
                tree_exp_mem(
                    (offset == 0)
                    ? tree_exp_temp(
                        r, r.temp_size,
                        translate_type(ar, info->program, expr->ex_type), ar)
                    : tree_exp_binop(
                        TREE_BINOP_PLUS,
                        tree_exp_temp(
                            r, r.temp_size,
                            translate_type(ar, info->program, expr->ex_type), ar),
                        tree_exp_const(
                            offset, ac_word_size,
                            tree_typ_ptr_diff(ar), ar),
                        ar
                    ),
                    arg_size,
                    translate_type(ar, info->program, arg->ex_type),
                    ar
                ),
                translate_un_ex(info, init_exp),
                ar
        );
        offset += arg_size;

        init_seq_tail = &((*init_seq_tail)->tst_seq_s2);
        *init_seq_tail = tree_stm_seq(init, NULL, ar);
    }
    // this loses some memory ... oh well
    *init_seq_tail = (*init_seq_tail)->tst_seq_s1;

    tree_exp_t* result = tree_exp_eseq(
            init_seq,
            tree_exp_temp(
                r, r.temp_size,
                translate_type(ar, info->program, expr->ex_type), ar),
            ar
    );
    return translate_ex(result);
}

static translate_exp_t* translate_expr_call(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    Arena_T ar = info->ret_arena;
    // TODO: if return type size is greater than 16 want to insert
    // temp binding with copy, and then pass as reference parameter
    assert(size_of_type(info->program, expr->ex_type) <= 2 * ac_word_size);

    tree_exp_t* translated_args = NULL;

    for (EX_LIST_IT(fnarg, expr->ex_fn_args)) {
        var arg_ex = translate_expr(info, frame, fnarg);
        var arg = translate_un_ex(info, arg_ex);

        translated_args = tree_exp_append(translated_args, arg);
    }

    tree_exp_t* result = tree_exp_call(
        tree_exp_name(expr->ex_fn_name, ar),
        translated_args,
        size_of_type(info->program, expr->ex_type),
        translate_type(ar, info->program, expr->ex_type),
        ac_calculate_ptr_maps(frame, expr->ex_fn_defd_vars, ar),
        ar
    );
    return translate_ex(result);
}

/*
 * Assign arg to the return location for the current function
 */
static tree_stm_t* assign_return(ac_frame_t* frame, tree_exp_t* arg, Arena_T a)
{
    if (arg->te_size <= ac_word_size) {
        temp_t t = frame->acf_target->tgt_ret0; // Take a COPY of acr_ret0
        t.temp_size = arg->te_size;
        return tree_stm_move(
            tree_exp_temp(t, t.temp_size, arg->te_type, a),
            arg,
            a
        );
    } else if (arg->te_size <= 2 * ac_word_size) {
        // If we have a mem reference, turn it into to
        // maybe
        // MOVE(temp t, arg)
        // MOVE(ret0, temp t)
        // MOVE(ret1, BINOP(PLUS, temp t, CONST(ac_word_size))

        tree_printf(stderr, "%E\n", arg);
        assert(0 && "TODO larger return sizes");
    } else {
        tree_printf(stderr, "%E\n", arg);
        assert(0 && "TODO larger return sizes");
    }
}

static translate_exp_t* translate_expr_return(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    /*
     * I guess this will be a case of jumping to a final label at the end
     * of the function... where the eiplogue is emitted?
     */

    Arena_T arena = info->ret_arena;
    tree_stm_t* result = unconditional_jump(info->function_end_label, arena);
    info->is_end_label_used = 1;

    if (expr->ex_ret_arg) {
        size_t ret_type_size = size_of_type(
                info->program, expr->ex_ret_arg->ex_type);
        var arg_ex = translate_expr(info, frame, expr->ex_ret_arg);
        var arg = translate_un_ex(info, arg_ex);

        assert(arg->te_size == ret_type_size);
        var move_stmt = assign_return(frame, arg, arena);
        result = tree_stm_seq(move_stmt, result, arena);
    }

    return translate_nx(result);
}

static translate_exp_t* translate_expr_break(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    /*
     * jump to the end label of the currently enclosing loop
     */
    tree_stm_t* result = unconditional_jump(
            info->current_loop_end, info->ret_arena);
    return translate_nx(result);
}

static translate_exp_t* translate_expr_loop(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    /*
     * start:
     *   s1
     *   ...
     *   s99
     *   goto start;
     * end:
     */
    sl_sym_t loop_start = temp_newlabel(info->temp_state);
    sl_sym_t loop_end = temp_newlabel(info->temp_state);

    /* saved the name of the enclosing loop end */
    sl_sym_t saved_end = info->current_loop_end;
    info->current_loop_end = loop_end;

    tree_stm_t* translated_stmts = tree_stm_label(loop_start, info->ret_arena);

    for (EX_LIST_IT(stmt, expr->ex_loop_body)) {
        tree_stm_t* s = translate_un_nx(
                info, translate_expr(info, frame, stmt));

        translated_stmts = tree_stm_seq(translated_stmts, s, info->ret_arena);
    }

    /* restore loop_end */
    info->current_loop_end = saved_end;

    tree_stm_t* result = tree_stm_seq(
            translated_stmts,
            tree_stm_label(loop_end, info->ret_arena), info->ret_arena);
    return translate_nx(result);
}

static translate_exp_t* translate_expr_deref(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    var arg_ex = translate_expr(info, frame, expr->ex_deref_arg);
    tree_exp_t* arg =
        translate_un_ex(info, arg_ex);

    size_t size = size_of_type(info->program, expr->ex_deref_arg->ex_type);
    assert(size > 0);
    assert(size != -1);
    tree_typ_t* type =
        translate_type(
                info->ret_arena, info->program, expr->ex_deref_arg->ex_type);

    tree_exp_t* result = tree_exp_mem(arg, size, type, info->ret_arena);
    return translate_ex(result);
}

static translate_exp_t* translate_expr_addrof(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    var arg_ex = translate_expr(info, frame, expr->ex_addrof_arg);
    tree_exp_t* arg =
        translate_un_ex(info, arg_ex);

    /*
     * 3 cases (until we add array subscript)
     *   * var
     *   * member
     *   * deref
     * in all 3, the result is a MEM(addr)
     * So! we just pop that off
     */
    assert(arg->te_tag == TREE_EXP_MEM);
    tree_exp_t* result = arg->te_mem_addr;
    return translate_ex(result);
}

static translate_exp_t* translate_expr_member(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    // I think we need access to the type information for the struct type
    // to know the offsets

    var arena = info->ret_arena;
    var struct_decl = expr->ex_composite->ex_type->ty_decl;

    size_t member_size = 0;
    tree_typ_t* member_type = NULL;
    int offset = 0;
    for (var mem = struct_decl->dl_params; mem; mem = mem->dl_list) {
        member_size = size_of_type(info->program, mem->dl_type);
        var member_alignment = alignment_of_type(info->program, mem->dl_type);
        offset = round_up_size(offset, member_alignment);

        if (mem->dl_name == expr->ex_member) {
            member_type =
                translate_type(arena, info->program, mem->dl_type);
            break;
        }

        offset += member_size;
    }
    assert(member_size > 0);
    assert(member_type != NULL);


    // Case 1: deref == (mem *addr*)
    // Case 2: var == (mem *addr*)
    // Case 3: member == (mem *addr*)
    // TODO: think about reg vars

    var base_ref_ex = translate_expr(info, frame, expr->ex_composite);
    tree_exp_t* base_ref = translate_un_ex(info, base_ref_ex);

    // The common case. The struct is in memory e.g. the stack or the heap
    if (base_ref->te_tag == TREE_EXP_MEM) {
        tree_exp_t* base_addr = base_ref->te_mem_addr;

        tree_exp_t* result = tree_exp_mem(
                (offset == 0)
                ? base_addr
                : tree_exp_binop(
                    TREE_BINOP_PLUS,
                    base_addr,
                    tree_exp_const(
                        offset, ac_word_size,
                        tree_typ_ptr_diff(arena), arena),
                    arena
                    ),
                member_size,
                member_type,
                arena
                );

        return translate_ex(result);
    }

    // The uncommon case: The struct fits in a register
    assert(base_ref->te_tag == TREE_EXP_TEMP);

    // Examples:
    //  struct X { a: int, b: int }
    //  struct X { a: int, b: bool, c: bool, d: bool, e: bool }
    //  ...

    // We have to build a mask and a shift.
    // Thing is... there are some nice bit manipulation instructions
    // definitely on arm. Maybe we can add some detection in the layer
    // below...

    assert(offset >= 0);
#define bytes2bits(size) (size * 8)
    int shift = bytes2bits(offset);
    uint64_t mask = (1 << bytes2bits(member_size)) - 1;
#undef bytes2bits

    tree_exp_t* result = tree_exp_binop(
            TREE_BINOP_AND,
            tree_exp_binop(
                TREE_BINOP_RSHIFT,
                base_ref,
                tree_exp_const(
                    shift, base_ref->te_size, base_ref->te_type, arena),
                arena
                ),
            tree_exp_const(
                mask, base_ref->te_size, base_ref->te_type, arena),
            arena
            );
    return translate_ex(result);
}

static translate_exp_t* translate_expr_if(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    Arena_T arena = info->ret_arena;
    // TODO: consider some special cases
    var condition = translate_un_cx(
            translate_expr(info, frame, expr->ex_if_cond));
    var cons = translate_un_ex(
            info, translate_expr(info, frame, expr->ex_if_cons));
    var alt = translate_un_ex(
            info, translate_expr(info, frame, expr->ex_if_alt));
    sl_sym_t tlabel = temp_newlabel(info->temp_state);
    sl_sym_t flabel = temp_newlabel(info->temp_state);
    sl_sym_t join = temp_newlabel(info->temp_state);

    size_t cons_sz = size_of_type(info->program, expr->ex_if_cons->ex_type);
    tree_typ_t* cons_ty =
        translate_type(arena, info->program, expr->ex_if_cons->ex_type);
    temp_t r = temp_newtemp(info->temp_state, cons_sz, tree_dispo_from_type(cons_ty));
    tree_exp_t* r_exp = tree_exp_temp(r, cons_sz, cons_ty, arena);

    // TODO: What about the void if/else expressions?
    // in those, we don't need this r_exp thing

    var res = lbf_call(condition, tlabel, flabel, arena);
    res = tree_stm_seq(res, tree_stm_label(tlabel, arena), arena);
    res = tree_stm_seq(res, tree_stm_move(r_exp, cons, arena), arena);
    res = tree_stm_seq(res, unconditional_jump(join, arena), arena);
    res = tree_stm_seq(res, tree_stm_label(flabel, arena), arena);
    res = tree_stm_seq(res, tree_stm_move(r_exp, alt, arena), arena);
    res = tree_stm_seq(res, unconditional_jump(join, arena), arena);
    res = tree_stm_seq(res, tree_stm_label(join, arena), arena);
    return translate_ex(tree_exp_eseq(res, r_exp, arena));
}

static translate_exp_t* translate_expr(
        translate_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
    switch (expr->ex_tag) {
        case SL_EXPR_VAR:
            return translate_expr_var(info, frame, expr);
        case SL_EXPR_INT:
            return translate_expr_int(info, frame, expr);
        case SL_EXPR_BOOL:
            return translate_expr_bool(info, frame, expr);
        case SL_EXPR_VOID:
            // Can happen, as an unspecified else branch
            return translate_expr_void(info, frame, expr);
        case SL_EXPR_BINOP:
            return translate_expr_binop(info, frame, expr);
        case SL_EXPR_LET:
            return translate_expr_let(info, frame, expr);
        case SL_EXPR_NEW:
            return translate_expr_new(info, frame, expr);
        case SL_EXPR_CALL:
            return translate_expr_call(info, frame, expr);
        case SL_EXPR_RETURN:
            return translate_expr_return(info, frame, expr);
        case SL_EXPR_BREAK:
            return translate_expr_break(info, frame, expr);
        case SL_EXPR_LOOP:
            return translate_expr_loop(info, frame, expr);
        case SL_EXPR_DEREF:
            return translate_expr_deref(info, frame, expr);
        case SL_EXPR_ADDROF:
            return translate_expr_addrof(info, frame, expr);
        case SL_EXPR_MEMBER:
            return translate_expr_member(info, frame, expr);
        case SL_EXPR_IF:
            return translate_expr_if(info, frame, expr);;
    }
}

static tree_stm_t* translate_decl(
        translate_info_t* info, ac_frame_t* frame, const sl_decl_t* decl)
{
    var ar = info->ret_arena;
    info->function_end_label = temp_newlabel(info->temp_state);

    // always have non-empty body
    assert(decl->dl_body);
    tree_stm_t* stmts = NULL;
    translate_exp_t* last_expr = NULL;
    for (sl_expr_t* e = decl->dl_body; e; e = e->ex_list) {
        if (last_expr) {
            var stmt = translate_un_nx(info, last_expr);
            if (!stmts) {
                stmts = stmt;
            } else {
                stmts = tree_stm_seq(stmts, stmt, ar);
            }
        }
        last_expr = translate_expr(info, frame, e);
    }

    var result_exp = (stmts)
        ? tree_exp_eseq(
                stmts, translate_un_ex(info, last_expr), ar)
        : translate_un_ex(info, last_expr);
    last_expr = NULL;

    var return_assignment = assign_return(frame, result_exp, ar);
    // assign the return value to the right registers and declare a label for
    // the end of the function
    if (info->is_end_label_used) {
        return tree_stm_seq(
                return_assignment,
                tree_stm_label(info->function_end_label, ar), ar
        );
    } else {
        return return_assignment;
    }
}

// TODO: change this to return the code and data fragments separately.
sl_fragment_t*
translate_program(
        Arena_T arena, temp_state_t* temp_state,
        const sl_decl_t* program, ac_frame_t* frames)
{
    // return some sort of list of functions, with each carrying a reference
    // to the activation record, and to the IR representation
    translate_info_t info = {
        .temp_state = temp_state, .program = program, .ret_arena = arena,
        .scratch = Arena_new(),
    };

    sl_fragment_t* result = NULL;

    const sl_decl_t* d;
    ac_frame_t* f;
    for (d = program, f = frames; d; d = d->dl_list) {
        assert(f); // d => f
        if (d->dl_tag == SL_DECL_FUNC) {
            var body = translate_decl(&info, f, d);
            body = proc_entry_exit_1(arena, temp_state, f, body);
            var frag = sl_code_fragment(body, f, arena);
            result = fr_append(result, frag);

            // the next frame will be for the next function, so iter
            f = f->acf_link;
        }
        Arena_free(info.scratch);
    }
    assert(!f);

    result = fr_append(result, info.string_fragments);

    Arena_dispose(&info.scratch);

    return result;
}
