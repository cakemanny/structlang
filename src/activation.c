#include "activation.h"
#include "mem.h" // xmalloc
#include <assert.h>

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

_Bool ac_debug = 0;

const struct ac_builtin_type {
    char* name;
    size_t alignment;
    size_t size;
    _Bool is_ptr;
} builtin_sizes[] = {
    { "int", 4, 4, 0 },
    { "bool", 1, 1, 0 },
    { "void", 0, 0, 0 },
};

const char* argument_reg_names[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
temp_t argument_regs[] = {
    {.temp_id = 7}, // rdi
    {.temp_id = 6}, // rsi
    {.temp_id = 2}, // rdx
    {.temp_id = 1}, // rcx
    {.temp_id = 8}, // r8
    {.temp_id = 9}, // r9
};
const char* return_registers[] = { "rax", "rdx" };
const char* frame_pointer = "rbp";

const char* registers[] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};
ac_registers_t register_temps = {
    .acr_sp = {.temp_id = 4}, // rsp
    .acr_fp = {.temp_id = 5}, // rbp
    .acr_ret0 = {.temp_id = 0}, // rax
    .acr_ret1 = {.temp_id = 2}, // rdx
};


typedef struct target {
    size_t word_size;
    size_t stack_alignment;
} target_t;

const target_t target_x86_64 = {
    .word_size = 8,
    .stack_alignment = 16,
};
const size_t ac_word_size = 8;

// We might unmake this const in future?
const target_t* const target = &target_x86_64;

static ac_frame_t* ac_frame_new()
{
    ac_frame_t* f = xmalloc(sizeof *f);
    f->acf_next_arg_offset = 16;
    f->acf_last_local_offset = 0;
    f->ac_frame_vars_end = &f->ac_frame_vars;
    f->acf_regs = &register_temps;
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
            struct ac_frame_var* v = xmalloc(sizeof *v);
            v->acf_tag = ACF_ACCESS_FRAME;
            v->acf_name = expr->ex_name;
            v->acf_size = size;
            v->acf_alignment = alignment_of_type(program, expr->ex_type_ann);
            v->acf_var_id = expr->ex_let_id;
            v->acf_offset = frame->acf_last_local_offset - size;
            while ((v->acf_offset % v->acf_alignment) != 0)
                v->acf_offset--;
            v->acf_is_formal = 0;
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

static void calculate_activation_record_decl_func(
        sl_decl_t* program, ac_frame_t* frame, sl_decl_t* decl)
{
    assert(decl->dl_tag == SL_DECL_FUNC);
    if (ac_debug) {
        fprintf(stderr, "calc activation for %s\n", decl->dl_name);
    }

    // space for return type
    sl_type_t* ret_type = decl->dl_type;
    size_t ret_type_size = size_of_type(program, ret_type);

    if (ret_type_size <= 8) {
        // result goes into RAX
    } else if (ret_type_size <= 16) {
        // result goes into RAX, RDX
    } else {
        // ... WTF, why do we have an assert 0, but then some code?
        // Need to allocate temporary for the return value?
        assert(0 && "TODO: larger return sizes");
        // The result is converted into a by-reference param
        struct ac_frame_var* v = xmalloc(sizeof *v);
        v->acf_tag = ACF_ACCESS_REG;
        v->acf_name = NULL; // no name TODO
        v->acf_size = target->word_size;
        v->acf_alignment = target->word_size;
        v->acf_var_id = -1; // no name
        v->acf_reg = argument_regs[frame->acf_next_arg_reg++];
        v->acf_is_formal = 1; // ... not really though ...
        v->acf_ptr_map = xmalloc(
            BitsetLen(num_words(ret_type_size)) * sizeof *v->acf_ptr_map);
        ptr_map_for_type(program, ret_type, v->acf_ptr_map, 0);

        ac_frame_append_var(frame, v);
    }

    for (sl_decl_t* p = decl->dl_params; p; p = p->dl_list) {
        sl_type_t* type = p->dl_type;
        size_t size = size_of_type(program, type);

        struct ac_frame_var* v = xmalloc(sizeof *v);
        v->acf_name = p->dl_name;
        v->acf_size = size;
        v->acf_alignment = alignment_of_type(program, type);
        v->acf_var_id = p->dl_var_id;
        v->acf_is_formal = 1;
        v->acf_ptr_map = xmalloc(
            BitsetLen(num_words(size)) * sizeof *v->acf_ptr_map);
        ptr_map_for_type(program, type, v->acf_ptr_map, 0);

        if (size <= 8 && frame->acf_next_arg_reg < NELEMS(argument_regs)) {
            // passed in register
            v->acf_tag = ACF_ACCESS_REG;
            v->acf_reg = argument_regs[frame->acf_next_arg_reg++];
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

    int frame_words = num_words(round_up_size(-frame->acf_last_local_offset, 16));
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
            fprintf(stderr, "setting frame ptr map from var %s\n", v->acf_name);
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

ac_frame_t* calculate_activation_records(sl_decl_t* program)
{
    if (ac_debug) {
        fprintf(stderr, "calculating activation records\n");
    }
    ac_frame_t* frame_list = NULL;
    for (sl_decl_t* d = program; d; d = d->dl_list) {
        if (d->dl_tag == SL_DECL_FUNC) {
            ac_frame_t* f = ac_frame_new();
            calculate_activation_record_decl_func(program, f, d);
            frame_list = ac_frame_append_frame(frame_list, f);
        }
    }
    return frame_list;
}
