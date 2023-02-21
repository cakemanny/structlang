#include "activation.h"
#include "x86_64.h"
#include "mem.h" // xmalloc
#include <assert.h>

#define var __auto_type

#define BitsetLen(len) (((len) + 63) / 64)
#define IsBitSet(x, i) (( (x)[(i)>>6] & (1ULL<<((i)&63)) ) != 0ULL)
#define SetBit(x, i) (x)[(i)>>6] |= (1ULL<<((i)&63))
#define ClearBit(x, i) (x)[(i)>>6] &= (1ULL<<((i)&63)) ^ 0xFFFFFFFFFFFFFFFFULL
// char 7
// short 15
// int  31
// int64 63
#define NELEMS(A) ((sizeof A) / sizeof A[0])

/*
Here we calculate the offsets of locals, arguments and results

for the moment, just consider x86_64

+---------------+
| ...           |
| arguments     | 16 +
+---------------+
| return addr   | 8
+---------------+
| prev %rbp     | 0 (%rbp)
+---------------+
| locals        | -8
| ...           |
+---------------+
| temporaries   |
| ...           |
+---------------+

How will our bitmaps work...
Two bitmaps, one for arguments, another for locals.

 */

bool ac_debug = 0;

const struct ac_builtin_type {
    char* name;
    size_t alignment;
    size_t size;
    bool is_ptr;
} builtin_sizes[] = {
    { "int", 4, 4, false },
    { "bool", 1, 1, false },
    { "void", 0, 0, false },
};

const size_t ac_word_size = 8;

static const target_t* target = &target_x86_64;

static ac_frame_t* ac_frame_new(
        sl_sym_t func_name, const target_t* target, Table_T temp_map)
{
    ac_frame_t* f = xmalloc(sizeof *f);
    f->acf_name = func_name;
    f->acf_next_arg_offset =
        // space for previous frame pointer and for return address
        2 * target->word_size;
    f->acf_last_local_offset = 0;
    f->ac_frame_vars_end = &f->ac_frame_vars;
    f->acf_target = target;
    f->acf_temp_map = temp_map;
    return f;
}

void ac_frame_free(ac_frame_t** pframe)
{
    ac_frame_t* frame = *pframe;
    while (frame->ac_frame_vars) {
        struct ac_frame_var* v = frame->ac_frame_vars;
        frame->ac_frame_vars = v->acf_list;
        if (v->acf_ptr_map) {
            free(v->acf_ptr_map);
            v->acf_ptr_map = NULL;
        }
        free(v);
    }
    if (frame->acf_locals_ptr_bitset) {
        free(frame->acf_locals_ptr_bitset);
        frame->acf_locals_ptr_bitset = NULL;
    }
    if (frame->acf_args_ptr_bitset) {
        free(frame->acf_args_ptr_bitset);
        frame->acf_args_ptr_bitset = NULL;
    }
    free(frame);
    *pframe = NULL;
}

static void ac_frame_append_var(ac_frame_t* frame, struct ac_frame_var* v)
{
    // ac_frame_vars_end points to where to the last point in the chain
    (*frame->ac_frame_vars_end) = v;
    frame->ac_frame_vars_end = &v->acf_list;
}

static ac_frame_t* ac_frame_append_frame(ac_frame_t* head, ac_frame_t* to_append)
{
    if (!head) {
        return to_append;
    }

    ac_frame_t* f = head;
    while (f->acf_link) {
        f = f->acf_link;
    }

    f->acf_link = to_append;
    return head;
}

static int round_up_size(int size, int multiple) __attribute__((const));
static int round_up_size(int size, int multiple)
{
    // fields, params and lets can't be void, so this should not happen
    assert(multiple > 0);
    return ((size + multiple - 1) / multiple) * multiple;
}

static size_t num_words(size_t num_bytes) __attribute__((const));
static size_t num_words(size_t num_bytes)
{
    return round_up_size(num_bytes, target->word_size) / target->word_size;
}

static const struct ac_builtin_type*
lookup_builtin(const sl_type_t* type)
{
    for (int i = 0; i < NELEMS(builtin_sizes); i++) {
        if (type->ty_name == symbol(builtin_sizes[i].name)) {
            return builtin_sizes + i;
        }
    }
    return NULL;
}

static const sl_decl_t* lookup_struct(
        const sl_decl_t* program, const sl_type_t* type)
{
    for (const sl_decl_t* decl = program; decl; decl = decl->dl_list) {
        if (decl->dl_tag == SL_DECL_STRUCT
                && decl->dl_name == type->ty_name) {
            return decl;
        }
    }
    fprintf(stderr, "type->name = %s\n", type->ty_name);
    assert(0 && "unknown type name");
}

size_t alignment_of_type(const sl_decl_t* program, sl_type_t* type)
{
    if (type->ty_alignment != -1) {
        return type->ty_alignment;
    }
    switch (type->ty_tag) {
        case SL_TYPE_NAME:
        {
            const struct ac_builtin_type* builtin = lookup_builtin(type);
            if (builtin) {
                // while we are here, assign the size too
                type->ty_size = builtin->size;
                return type->ty_alignment = builtin->alignment;
            }
            const sl_decl_t* decl = lookup_struct(program, type);
            size_t alignment = 0;
            for (const sl_decl_t* field = decl->dl_params; field;
                    field = field->dl_list) {
                size_t field_alignment =
                    alignment_of_type(program, field->dl_type);
                if (field_alignment > alignment) {
                    alignment = field_alignment;
                }
            }
            return type->ty_alignment = alignment;
        }
        case SL_TYPE_PTR:
            return type->ty_alignment = target->word_size;
        case SL_TYPE_ARRAY:
            // alignment of its elements?
            assert(0 && "not implemented, alignment_of_type, array");
        case SL_TYPE_FUNC:
            return type->ty_alignment = target->word_size;
    }
    fprintf(stderr, "type->ty_tag = %d (0x%x)\n", type->ty_tag, type->ty_tag);
    assert(0 && " missing case");
}

// TODO: we should scan through the program and calculate the types as a
// pre-step maybe?
/*
 * The size of the type as stored in the stack frame. Including padding for
 * alignment...
 */
size_t size_of_type(const sl_decl_t* program, sl_type_t* type)
{
    if (type->ty_size != -1) {
        return type->ty_size;
    }
    switch (type->ty_tag) {
        case SL_TYPE_NAME:
        {
            const struct ac_builtin_type* builtin = lookup_builtin(type);
            if (builtin) {
                // while we are here, assign the alignment too
                type->ty_alignment = builtin->alignment;
                return type->ty_size = builtin->size;
            }
            const sl_decl_t* decl = lookup_struct(program, type);
            size_t total_size = 0;
            for (const sl_decl_t* field = decl->dl_params; field;
                    field = field->dl_list) {
                size_t field_alignment =
                    alignment_of_type(program, field->dl_type);
                total_size = round_up_size(total_size, field_alignment);
                total_size += size_of_type(program, field->dl_type);
            }
            size_t alignment = alignment_of_type(program, type);
            total_size = round_up_size(total_size, alignment);
            return type->ty_size = total_size;
        }
        case SL_TYPE_PTR:
            return type->ty_size = target->word_size;
        case SL_TYPE_ARRAY:
            assert(0 && "not implemented, size_of_type, array");
        case SL_TYPE_FUNC:
            // unless we add first-class functions
            assert(0 && "not implemented, size_of_type, function");
    }
    fprintf(stderr, "type->ty_tag = %d (0x%x)\n", type->ty_tag, type->ty_tag);
    assert(0 && "missing case");
}

static void ptr_map_for_type(
        const sl_decl_t* program, sl_type_t* type, uint64_t* map, int offset)
{
    switch (type->ty_tag) {
        case SL_TYPE_NAME:
        {
            const struct ac_builtin_type* builtin = lookup_builtin(type);
            if (builtin) {
                if (builtin->is_ptr) {
                    SetBit(map, offset);
                }
                return;
            }
            const sl_decl_t* decl = lookup_struct(program, type);
            size_t total_size = 0;
            for (const sl_decl_t* field = decl->dl_params; field;
                    field = field->dl_list) {
                size_t field_alignment =
                    alignment_of_type(program, field->dl_type);

                total_size = round_up_size(total_size, field_alignment);

                if (field_alignment >= target->word_size) {
                    // possible to be a pointer
                    ptr_map_for_type(program, type, map,
                            offset + num_words(total_size));
                }

                total_size += size_of_type(program, field->dl_type);
            }
            return;
        }
        case SL_TYPE_PTR:
            SetBit(map, offset);
            return;
        case SL_TYPE_ARRAY:
            assert(0 && "not implemented, size_of_type, array");
        case SL_TYPE_FUNC:
            // unless we add first-class functions
            assert(0 && "not implemented, size_of_type, function");
    }
    fprintf(stderr, "type->ty_tag = %d (0x%x)\n", type->ty_tag, type->ty_tag);
    assert(0 && "missing case");
}

static void calculate_activation_record_expr(
        const sl_decl_t* program, ac_frame_t* frame, sl_expr_t* expr)
{
#define EX_LIST_IT(it, head) sl_expr_t* it = (head); it; it = it->ex_list
#define recur(e) calculate_activation_record_expr(program, frame, e)
    switch (expr->ex_tag)
    {
        /* interesting case */
        case SL_EXPR_LET:
        {
            recur(expr->ex_init);
            size_t size = size_of_type(program, expr->ex_type_ann);
            assert(size > 0 && "zero-size let-bound variable");
            struct ac_frame_var* v = xmalloc(sizeof *v);
            v->acf_tag = ACF_ACCESS_FRAME;
            v->acf_varname = expr->ex_name;
            v->acf_size = size;
            v->acf_alignment = alignment_of_type(program, expr->ex_type_ann);
            v->acf_var_id = expr->ex_let_id;
            v->acf_offset = frame->acf_last_local_offset - size;
            while ((v->acf_offset % v->acf_alignment) != 0)
                v->acf_offset--;
            v->acf_is_formal = false;
            v->acf_ptr_map = xmalloc(
                    BitsetLen(num_words(size)) * sizeof *v->acf_ptr_map);
            ptr_map_for_type(program, expr->ex_type_ann, v->acf_ptr_map, 0);

            frame->acf_last_local_offset = v->acf_offset;
            ac_frame_append_var(frame, v);
            return;
        }
        /* recursive cases */
        case SL_EXPR_INT: /* fall through */
        case SL_EXPR_BOOL: /* fall through */
        case SL_EXPR_VOID:
            return;
        case SL_EXPR_BINOP:
            recur(expr->ex_left);
            recur(expr->ex_right);
            return;
        case SL_EXPR_CALL:
            // TODO: if return type size is greater than 16 want to insert
            // temp binding with copy, and then pass as reference parameter
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
            if (expr->ex_ret_arg) {
                recur(expr->ex_ret_arg);
            }
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
            // TODO: consider that this forces a var to be frame rather than
            // reg
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
#undef recur
#undef EX_LIST_IT
    fprintf(stderr, "expr->ex_tag = %d (0x%x)\n", expr->ex_tag, expr->ex_tag);
    assert(0 && " missing case");
}

int ac_frame_words(ac_frame_t* frame) {
    var target = frame->acf_target;

    return num_words(round_up_size(
                -frame->acf_last_local_offset + frame->acf_outgoing_arg_bytes,
                target->stack_alignment));
}

static void calculate_activation_record_decl_func(
        sl_decl_t* program, ac_frame_t* frame, sl_decl_t* decl)
{
    assert(decl->dl_tag == SL_DECL_FUNC);
    if (ac_debug) {
        fprintf(stderr, "calc activation for %s\n", decl->dl_name);
    }
    var target = frame->acf_target;

    // space for return type
    sl_type_t* ret_type = decl->dl_type;
    size_t ret_type_size = size_of_type(program, ret_type);

    if (ret_type_size <= 8) {
        // result goes into RAX
    } else if (ret_type_size <= 16) {
        // result goes into RAX, RDX
    } else {
        /// tbh we should just have a rewrite stage that removes this case

        // ... WTF, why do we have an assert 0, but then some code?
        // Need to allocate temporary for the return value?
        assert(0 && "TODO: larger return sizes");
        // The result is converted into a by-reference param
        struct ac_frame_var* v = xmalloc(sizeof *v);
        v->acf_tag = ACF_ACCESS_REG;
        v->acf_varname = NULL; // no name TODO
        v->acf_size = target->word_size;
        v->acf_alignment = target->word_size;
        v->acf_var_id = -1; // no name
        v->acf_reg = target->arg_registers.elems[frame->acf_next_arg_reg++];
        v->acf_reg.temp_size = target->word_size;
        v->acf_is_formal = true; // ... not really though ...
        v->acf_ptr_map = xmalloc(
            BitsetLen(num_words(ret_type_size)) * sizeof *v->acf_ptr_map);
        ptr_map_for_type(program, ret_type, v->acf_ptr_map, 0);

        ac_frame_append_var(frame, v);
    }

    for (sl_decl_t* p = decl->dl_params; p; p = p->dl_list) {
        sl_type_t* type = p->dl_type;
        size_t size = size_of_type(program, type);
        assert(size > 0 && "zero-size parameter");

        struct ac_frame_var* v = xmalloc(sizeof *v);
        v->acf_varname = p->dl_name;
        v->acf_size = size;
        v->acf_alignment = alignment_of_type(program, type);
        v->acf_var_id = p->dl_var_id;
        v->acf_is_formal = true;
        v->acf_ptr_map = xmalloc(
            BitsetLen(num_words(size)) * sizeof *v->acf_ptr_map);
        ptr_map_for_type(program, type, v->acf_ptr_map, 0);

        if (size <= 8 && frame->acf_next_arg_reg < target->arg_registers.length) {
            // passed in register
            v->acf_tag = ACF_ACCESS_REG;
            v->acf_reg = target->arg_registers.elems[frame->acf_next_arg_reg++];
            v->acf_reg.temp_size = size;
        } else {
            // Add formal parameter
            v->acf_tag = ACF_ACCESS_FRAME;
            v->acf_offset = round_up_size(
                    frame->acf_next_arg_offset, v->acf_alignment);
            // what about frame args after arg 7 which are not 64-bit aligned
            frame->acf_next_arg_offset += size;
        }

        ac_frame_append_var(frame, v);
    }

    for (sl_expr_t* e = decl->dl_body; e; e = e->ex_list) {
        // Add space for locals
        // And maybe temporaries
        calculate_activation_record_expr(program, frame, e);
    }

    // Now, scan through the frame vars and calculate a bitset showing where
    // the pointers in the frame are

    int frame_words = ac_frame_words(frame);

    if (ac_debug) {
        fprintf(stderr, "frame_words = %d\n", frame_words);
    }
    frame->acf_locals_ptr_bitset = xmalloc(
            BitsetLen(frame_words) * sizeof *frame->acf_locals_ptr_bitset);
    uint64_t* const local_bs = frame->acf_locals_ptr_bitset;
    if (ac_debug) {
        fprintf(stderr, "local_bs = %p\n", local_bs);
    }

    int args_words = num_words(frame->acf_next_arg_offset);
    if (ac_debug) {
        fprintf(stderr, "args_words = %d\n", args_words);
    }
    frame->acf_args_ptr_bitset = xmalloc(
            BitsetLen(args_words) * sizeof *frame->acf_args_ptr_bitset);
    uint64_t* const arg_bs = frame->acf_args_ptr_bitset;
    if (ac_debug) {
        fprintf(stderr, "args_bs = %p\n", arg_bs);
    }


    for (struct ac_frame_var* v = frame->ac_frame_vars; v; v = v->acf_list) {
        if (ac_debug) {
            fprintf(stderr, "setting frame ptr map from var %s\n", v->acf_varname);
            fprintf(stderr, "v->acf_size = %zu\n", v->acf_size);
            fprintf(stderr, "v->acf_alignment = %zu\n", v->acf_alignment);
            fprintf(stderr, "v->acf_ptr_map = %p\n", v->acf_ptr_map);
        }
        if (v->acf_tag == ACF_ACCESS_FRAME) {
            // if, is ptr type
            if (v->acf_alignment >= target->word_size) {
                for (int i = 0; i < num_words(v->acf_size); i++) {
                    if (ac_debug) { fprintf(stderr, "i = %d\n", i); }
                    if (IsBitSet(v->acf_ptr_map, i)) {
                        if (v->acf_offset < 0) {
                            if (ac_debug) {
                                fprintf(stderr, "frame_words = %d\n", frame_words);
                                fprintf(stderr, "num_words(v->acf_offset) = %zu\n", num_words(-v->acf_offset));
                            }
                            SetBit(local_bs,
                                   frame_words + num_words(-v->acf_offset));
                        } else {
                            SetBit(arg_bs, num_words(v->acf_offset) + i);
                        }
                    }
                }
            }
        }
    }
}

ac_frame_t* calculate_activation_records(enum target_type target_tag, sl_decl_t* program)
{
    if (ac_debug) {
        fprintf(stderr, "calculating activation records\n");
    }

    Table_T temp_map =
        (target_tag == TARGET_X86_64) ? x86_64_temp_map()
        : arm64_temp_map();

    // !!
    target =
        (target_tag == TARGET_X86_64) ? &target_x86_64
        : &target_arm64;

    ac_frame_t* frame_list = NULL;
    for (sl_decl_t* d = program; d; d = d->dl_list) {
        if (d->dl_tag == SL_DECL_FUNC) {
            ac_frame_t* f = ac_frame_new(d->dl_name, target, temp_map);
            calculate_activation_record_decl_func(program, f, d);
            frame_list = ac_frame_append_frame(frame_list, f);
        }
    }
    return frame_list;
}

/*
 * Creates some space in the frame to store a temporary
 */
struct ac_frame_var* ac_spill_temporary(ac_frame_t* frame)
{
    var target = frame->acf_target;
    size_t size = target->word_size;
    struct ac_frame_var* v = xmalloc(sizeof *v);
    v->acf_tag = ACF_ACCESS_FRAME;
    v->acf_varname = NULL; // ?? t_n
    v->acf_size = size;
    v->acf_alignment = target->word_size;
    v->acf_var_id = -1;
    v->acf_offset = frame->acf_last_local_offset - size;
    while ((v->acf_offset % v->acf_alignment) != 0)
        v->acf_offset--;
    v->acf_is_formal = false;
    // TODO: think about the ptr_map stuff ...

    frame->acf_last_local_offset = v->acf_offset;
    ac_frame_append_var(frame, v);
    return v;
}

/*
 * Ensures that at least required_bytes have been added to the
 * activation record for function call arguments that are sent on
 * the stack. i.e. words 9 and above (caveats not considered).
 */
void reserve_outgoing_arg_space(ac_frame_t* frame, size_t required_bytes)
{
    // TODO: maybe this function should calculate the space and alignment
    // etc?
    assert(required_bytes == round_up_size(required_bytes, target->stack_alignment));

    if (frame->acf_outgoing_arg_bytes < required_bytes) {
        frame->acf_outgoing_arg_bytes = required_bytes;
    }
}

tree_stm_t* proc_entry_exit_1(
        temp_state_t* temp_state, ac_frame_t* frame, tree_stm_t* body)
{
    // 1. we have to move each incoming register parameter to where it
    // is seen from in the function

    // 2. If we are not doing spilling, then we make room in the frame for
    // all callee-save registers and add instructions here to save them at
    // the start and to restore them at the end.
    // If we are doing spilling, then we save them to and restore them from
    // temporaries.

    // 1. Move register args to temps

    tree_stm_t* arg_moves = NULL;
    for (var v = frame->ac_frame_vars; v; v = v->acf_list) {
        if (v->acf_is_formal && v->acf_tag == ACF_ACCESS_REG) {
            temp_t param_reg = v->acf_reg;
            v->acf_reg = temp_newtemp(temp_state, v->acf_size);
            var move = tree_stm_move(
                    tree_exp_temp(v->acf_reg, v->acf_size), // <- dest
                    tree_exp_temp(param_reg, v->acf_size)); // <- src

            arg_moves = (arg_moves) ? tree_stm_seq(arg_moves, move) : move;
        }
    }
    if (arg_moves) {
        body = tree_stm_seq(arg_moves, body);
    }

    // 2. Save callee saves
    const target_t* target = frame->acf_target;
    const size_t num_callee_saves = target->callee_saves.length;
    const size_t word_size = target->word_size;
    temp_t temps_for_callee_saves[num_callee_saves];
    for (int i = 0; i < num_callee_saves; i++) {
        temps_for_callee_saves[i] = temp_newtemp(temp_state, word_size);
    }

    tree_stm_t* saves = NULL;
    for (int i = 0; i < num_callee_saves; i++) {
        var dst_access = tree_exp_temp(temps_for_callee_saves[i], word_size);
        var src_access = tree_exp_temp(target->callee_saves.elems[i], word_size);
        var move = tree_stm_move(dst_access, src_access);
        saves = (saves) ? tree_stm_seq(saves, move) : move;
    }

    tree_stm_t* restores = NULL;
    for (int i = 0; i < num_callee_saves; i++) {
        var dst_access = tree_exp_temp(target->callee_saves.elems[i], word_size);
        var src_access = tree_exp_temp(temps_for_callee_saves[i], word_size);
        var move = tree_stm_move(dst_access, src_access);
        restores = (restores) ? tree_stm_seq(restores, move) : move;
    }

    body = tree_stm_seq(tree_stm_seq(saves, body), restores);

    return body;
}
