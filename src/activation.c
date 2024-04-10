#include "activation.h"
#include "x86_64.h"
#include "mem.h" // xmalloc
#include "translate.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

#define var __auto_type

#define BitsetLen(len) (((len) + 63) / 64)
#define IsBitSet(x, i) (( (x)[(i)>>6] & (1ULL<<((i)&63)) ) != 0ULL)
#define SetBit(x, i) (x)[(i)>>6] |= (1ULL<<((i)&63))
#define ClearBit(x, i) (x)[(i)>>6] &= (1ULL<<((i)&63)) ^ 0xFFFFFFFFFFFFFFFFULL
#define BitsetBytes(len) (sizeof(uint64_t) * BitsetLen(len))
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

typedef struct act_info_t {
    const sl_decl_t* program;
    temp_state_t* temp_state;
    Arena_T frag_arena; // for permanent tree_ allocations
} act_info_t;

// __builtin_debugtrap();
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

static temp_ptr_disposition_t ptr_disp_of_type(const sl_type_t* type)
{
    switch (type->ty_tag) {
        case SL_TYPE_NAME: return TEMP_DISP_NOT_PTR;
        case SL_TYPE_PTR: return TEMP_DISP_PTR;
        case SL_TYPE_ARRAY:
            assert(!"not implemented, size_of_type, array");
        case SL_TYPE_FUNC:
            assert(!"not implemented, size_of_type, function");
    }
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
                    ptr_map_for_type(program, field->dl_type, map,
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


char* ac_record_descriptor_for_type(
        Arena_T arena, const sl_decl_t* program, sl_type_t* type)
{
    size_t size = size_of_type(program, type);

    unsigned long nwords = num_words(size);
    // +1 for the null terminator
    char* buf = Arena_alloc(arena, nwords + 1, __FILE__, __LINE__);

    // for now we just reuse the ptr map logic
    uint64_t* ptr_map = xmalloc(BitsetBytes(nwords));
    ptr_map_for_type(program, type, ptr_map, 0);
    for (int i = 0; i < nwords; i++) {
        if (IsBitSet(ptr_map, i)) {
            buf[i] = 'p';
        } else {
            buf[i] = 'n';
        }
    }
    free(ptr_map);
    return buf;
}


static void calculate_activation_record_expr(
        act_info_t* info, ac_frame_t* frame, sl_expr_t* expr)
{
#define EX_LIST_IT(it, head) sl_expr_t* it = (head); it; it = it->ex_list
#define recur(e) calculate_activation_record_expr(info, frame, e)
    switch (expr->ex_tag)
    {
        /* interesting case */
        case SL_EXPR_LET:
        {
            recur(expr->ex_init);
            size_t size = size_of_type(info->program, expr->ex_type_ann);
            assert(size > 0 && "zero-size let-bound variable");
            struct ac_frame_var* v = xmalloc(sizeof *v);
            v->acf_tag = ACF_ACCESS_FRAME;
            v->acf_varname = expr->ex_name;
            v->acf_size = size;
            v->acf_alignment = alignment_of_type(info->program, expr->ex_type_ann);
            v->acf_var_id = expr->ex_let_id;
            v->acf_offset = frame->acf_last_local_offset - size;
            while ((v->acf_offset % v->acf_alignment) != 0)
                v->acf_offset--;
            v->acf_is_formal = false;
            v->acf_ptr_map = xmalloc(BitsetBytes(num_words(size)));
            ptr_map_for_type(info->program, expr->ex_type_ann, v->acf_ptr_map, 0);
            v->acf_spilled.temp_id = -1;
            v->acf_stored.temp_id = -1;

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

int ac_frame_words(const ac_frame_t* frame) {
    var target = frame->acf_target;

    return num_words(round_up_size(
                -frame->acf_last_local_offset + frame->acf_outgoing_arg_bytes,
                target->stack_alignment));
}
#if 0
static int num_padding_words(const ac_frame_t* frame) {
    return ac_frame_words(frame) - num_words(-frame->acf_last_local_offset);
}
#endif

/*
 * function arguments are moved from the machine register into a temporary
 * so that the register is free to perform any duties it needs to (e.g. being
 * an argument to a function called by this function.).
 */
static temp_t assign_temporary_for_reg(
        act_info_t* info, ac_frame_t* frame, temp_t reg, size_t size,
        temp_ptr_disposition_t ptr_dispo, tree_typ_t* type)
{
    temp_t param_reg = reg;
    param_reg.temp_size = size;
    temp_t temp = temp_newtemp(info->temp_state, size, ptr_dispo);
    var move = tree_stm_move(
            tree_exp_temp(temp, size, type, info->frag_arena), // <- dest
            tree_exp_temp(param_reg, size, type, info->frag_arena)); // <- src

    if (frame->acf_arg_moves == NULL) {
        frame->acf_arg_moves = move;
    } else {
        frame->acf_arg_moves = tree_stm_seq(frame->acf_arg_moves, move);
    }
    return temp;
}

static void
calculate_activation_record_decl_func(
        act_info_t* info, ac_frame_t* frame, sl_decl_t* decl)
{
    assert(decl->dl_tag == SL_DECL_FUNC);
    if (ac_debug) {
        fprintf(stderr, "calc activation for %s\n", decl->dl_name);
    }
    var target = frame->acf_target;

    // space for return type
    sl_type_t* ret_type = decl->dl_type;
    size_t ret_type_size = size_of_type(info->program, ret_type);

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
        v->acf_ptr_map = xmalloc(BitsetBytes(num_words(ret_type_size)));
        ptr_map_for_type(info->program, ret_type, v->acf_ptr_map, 0);
        v->acf_spilled.temp_id = -1;
        v->acf_stored.temp_id = -1;

        ac_frame_append_var(frame, v);
    }

    for (sl_decl_t* p = decl->dl_params; p; p = p->dl_list) {
        sl_type_t* type = p->dl_type;
        size_t size = size_of_type(info->program, type);
        assert(size > 0 && "zero-size parameter");

        struct ac_frame_var* v = xmalloc(sizeof *v);
        v->acf_varname = p->dl_name;
        v->acf_size = size;
        v->acf_alignment = alignment_of_type(info->program, type);
        v->acf_var_id = p->dl_var_id;
        v->acf_is_formal = true;
        v->acf_ptr_map = xmalloc(BitsetBytes(num_words(size)));
        ptr_map_for_type(info->program, type, v->acf_ptr_map, 0);
        v->acf_spilled.temp_id = -1;
        v->acf_stored.temp_id = -1;

        if (size <= 8 && frame->acf_next_arg_reg < target->arg_registers.length) {
            // passed in register
            v->acf_tag = ACF_ACCESS_REG;
            v->acf_reg = assign_temporary_for_reg(info, frame,
                    target->arg_registers.elems[frame->acf_next_arg_reg++],
                    size, ptr_disp_of_type(type),
                    translate_type(info->frag_arena, info->program, type));
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
        calculate_activation_record_expr(info, frame, e);
    }
}


ac_frame_t*
calculate_activation_records(
        Arena_T frag_arena, const target_t* target, temp_state_t* temp_state,
        sl_decl_t* program)
{
    if (ac_debug) {
        fprintf(stderr, "calculating activation records\n");
    }

    Table_T temp_map = target->tgt_temp_map();

    ac_frame_t* frame_list = NULL;
    for (sl_decl_t* d = program; d; d = d->dl_list) {
        if (d->dl_tag == SL_DECL_FUNC) {
            ac_frame_t* f = ac_frame_new(d->dl_name, target, temp_map);
            act_info_t info = {
                .program = program,
                .temp_state = temp_state,
                .frag_arena = frag_arena,
            };
            calculate_activation_record_decl_func(&info, f, d);
            frame_list = ac_frame_append_frame(frame_list, f);
        }
    }
    return frame_list;
}


static bool in_defined_vars(int needle, int* defined_vars)
{
    for (int* it = defined_vars; *it > 0; it++) {
        if (needle == *it) {
            return true;
        }
    }
    return false;
}

static void
debug_print_frame(ac_frame_t* frame)
{
    // let's abuse some knowledge that we add arguments first
    // and then the locals

    int max_offset = 0;
    int min_offset = 0;

    for (struct ac_frame_var* v = frame->ac_frame_vars; v; v = v->acf_list) {
        if (v->acf_tag == ACF_ACCESS_FRAME) {
            max_offset = (v->acf_offset > max_offset) ? v->acf_offset : max_offset;
            min_offset = (v->acf_offset < min_offset) ? v->acf_offset : min_offset;
        }
    }

    fprintf(stderr, "  %s\n", frame->acf_name);
    int last_offset = max_offset + 8;
    for (struct ac_frame_var* v = frame->ac_frame_vars; v; v = v->acf_list) {
        if (v->acf_tag == ACF_ACCESS_FRAME) {
            if (last_offset > 0 && v->acf_offset < 0) {
            fprintf(stderr, "+--------------------------------------+\n");
            fprintf(stderr, "| return addr                          | 8\n");
            fprintf(stderr, "+--------------------------------------+\n");
            fprintf(stderr, "| previous FP                          | 0 (FP)\n");
            }

            fprintf(stderr, "+--------------------------------------+\n");
            for (int i = 0; i < num_words(v->acf_size) - 1; i++) {
                fprintf(stderr, "|                                      |\n");
            }
            if (v->acf_varname != NULL) {
                fprintf(stderr, "| %36s | %d \n", v->acf_varname, v->acf_offset);
            } else {
                fprintf(stderr, "| %36d | %d \n", v->acf_stored.temp_id, v->acf_offset);
            }

            last_offset = v->acf_offset;
        }
    }
    fprintf(stderr, "+--------------------------------------+\n\n");

}

/*
 * defined_vars is an array of var_id s that are in scope at the call site
 * (call or new)
 */
ac_frame_map_t* ac_calculate_ptr_maps(ac_frame_t* frame, int* defined_vars) {
    // Now, scan through the frame vars and calculate a bitset showing where
    // the pointers in the frame are

    ac_frame_map_t* frame_map = xmalloc(sizeof *frame_map);
    frame_map->acfm_frame = frame;

    if (ac_debug) {
        debug_print_frame(frame);
    }

    // We ignore the padding at this point, as we will include it when
    // extending this with spill data.
    // We have to create it initially now while we have the defined vars.
    int num_local_words =
        frame_map->acfm_num_local_words =
        num_words(-frame->acf_last_local_offset);
    frame_map->acfm_locals = xmalloc(BitsetBytes(num_local_words));

    int args_words = num_words(frame->acf_next_arg_offset);
    frame_map->acfm_num_arg_words = args_words;

    frame_map->acfm_args = xmalloc(BitsetBytes(args_words));


    for (struct ac_frame_var* v = frame->ac_frame_vars; v; v = v->acf_list) {
        if (v->acf_tag == ACF_ACCESS_FRAME
                // if can be ptr type
                && v->acf_alignment >= target->word_size
                // skip if the local is not in scope
                && in_defined_vars(v->acf_var_id, defined_vars)
        ) {
            assert(num_words(v->acf_size) <= 1 && "code below is broken");

            for (int i = 0; i < num_words(v->acf_size); i++) {
                if (IsBitSet(v->acf_ptr_map, i)) {
                    if (v->acf_offset < 0) {
                        SetBit(frame_map->acfm_locals,
                                num_local_words - num_words(-v->acf_offset) + i);
                        /*                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^
                         * it's best to think of this as: + offset
                         */
                    } else {
                        SetBit(frame_map->acfm_args, num_words(v->acf_offset) + i);
                    }
                }
            }
        }
    }
    return frame_map;
}

void ac_frame_map_free(ac_frame_map_t** pfm)
{
    var fm = *pfm;
    if (fm->acfm_args)
        free(fm->acfm_args);
    if (fm->acfm_locals)
        free(fm->acfm_locals);
    if (fm->acfm_spills)
        free(fm->acfm_spills);
    fm->acfm_args = fm->acfm_locals = fm->acfm_spills = NULL;

    fm->acfm_frame = NULL; // not owned by us, so we don't free

    free(fm);
    *pfm = NULL;
}

/*
 * Creates some space in the frame to store a temporary.
 * Called during register allocation.
 */
struct ac_frame_var* ac_spill_temporary(ac_frame_t* frame, temp_t t)
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

    /*
     * Idea: Track that it was a spill, and which temporary.
     * After final allocation, we will know which register is being spilled
     */
    v->acf_spilled = t;

    /*
     * The register allocator will fill this in when spilling, and we'll use
     * the allocation to work out what register it ends up being.
     */
    v->acf_stored.temp_id = -1; // to be filled in in reg_alloc

    frame->acf_last_local_offset = v->acf_offset;
    ac_frame_append_var(frame, v);
    return v;
}

static const char*
temp_dispo_str(temp_t t) // XXX: dup
{
    switch (t.temp_ptr_dispo) {
        case TEMP_DISP_PTR: return "*";
        case TEMP_DISP_NOT_PTR: return "";
        case TEMP_DISP_INHERIT: return "^";
    }
    return "!!!!!!!";
}

static bool
temp_eq(temp_t a, temp_t b) // XXX: dup
{
    return a.temp_id == b.temp_id;
}

static bool
temp_list_contains(const temp_list_t* haystack, temp_t temp) // XXX: dup
{
    for (var h = haystack; h; h = h->tmp_list) {
        if (temp_eq(h->tmp_temp, temp)) {
            return true;
        }
    }
    return false;
}

static int
reg_idx_for_name(const ac_frame_t* frame, const char* reg_name)
{
    // O(1)
    const int num_registers = Table_length(frame->acf_temp_map);
    int i = 0;
    for (i = 0; i < num_registers; i++) {
        // abuses knowledge that these are from the same place
        if (reg_name == frame->acf_target->register_names[i]) {
            break;
        }
    }
    return i;
}

/*
 * Given the following program
 *
 *   fn main() -> int {
 *    let x: *int = _; x
 *   }
 *
 * We expect two words of local space
 *
 *   +--------------------------+
 *   | x                        | -8
 *   +--------------------------+
 *   | (padding for alignment)  | -16
 *   +--------------------------+
 *
 * The locals bitmap shall be 0b10.
 * To think about this correctly, it's useful to rotate the block into
 * an array
 *
 *   { <padding>, x }
 *
 * we set a bit for x at index 1, and not for padding at index 0.
 * And we have to remember that binary reads from right to left.
 */

/*
 * After having gone through register allocation and now knowing which
 * temporaries have been spilled we recalculate the length of the locals
 * space and set bits in the bitmap to indicate
 */
void ac_extend_frame_map_for_spills(
        ac_frame_map_t* frame_map, temp_list_t* spill_live_outs,
        Table_T allocation)
{
    const ac_frame_t* frame = frame_map->acfm_frame;

    //
    // It's best to have a second bitmap for inherited disposition
    // instead of trying use a 2-bit-item map.
    //
    // i.e. we need to extend num_local_words, realloc, and memmove the
    // existing bitmap
    //
    //   +--------+    +--------------------------+
    //   | locals | => | padding, spills   locals | <- bitset for ptr
    //   +--------+    +-----------------+--------+
    //               + | padding, spills | <- bitset for inherits
    //                 +-----------------+
    //

    const int frame_words = ac_frame_words(frame);
    const int old_num_local_words = frame_map->acfm_num_local_words;

    // spill_words includes the alignment padding.
    const int spill_words = frame_words - old_num_local_words;
    frame_map->acfm_num_local_words = frame_words;

    if (spill_words > 0) {
        var locals_old = frame_map->acfm_locals;
        frame_map->acfm_locals = xmalloc(BitsetBytes(frame_words));

        // copy old, shifting by spill_words bits
        for (int i = 0; i < old_num_local_words; i++) {
            if (IsBitSet(locals_old, i)) {
                SetBit(frame_map->acfm_locals, i + spill_words);
            }
        }
        free(locals_old);
    }

    frame_map->acfm_spills = xmalloc(BitsetBytes(spill_words));
    frame_map->acfm_num_spill_words = spill_words;

    int spill_reg_idx = 0;

    for (struct ac_frame_var* v = frame->ac_frame_vars; v; v = v->acf_list) {
        if (v->acf_tag == ACF_ACCESS_FRAME
                // is a spill
                && v->acf_spilled.temp_id >= 0
                && v->acf_var_id == -1
                // was live during this call / allocation
                && temp_list_contains(spill_live_outs, v->acf_spilled)
        ) {
            const char* reg_name = Table_get(allocation, &v->acf_stored);
            if (reg_name == NULL) {
                reg_name = "";
            }

            if (ac_debug) {
                fprintf(stderr, "XXX(offset:%d): %d%s :: %s\n",
                        v->acf_offset,
                        v->acf_spilled.temp_id, temp_dispo_str(v->acf_spilled),
                        reg_name);
            }

            int bit_to_set = (frame_words - num_words(-v->acf_offset));
            switch (v->acf_spilled.temp_ptr_dispo) {
                case TEMP_DISP_PTR:
                    SetBit(frame_map->acfm_locals, bit_to_set);
                    break;
                case TEMP_DISP_NOT_PTR:
                    break;
                case TEMP_DISP_INHERIT:
                {
                    // asserts that we always know which register was
                    // spilled.
                    assert(strlen(reg_name) > 0);
                    uint8_t reg_idx = reg_idx_for_name(frame, reg_name);
                    assert(reg_idx != Table_length(frame->acf_temp_map));

                    // we should only need to store the spilled registers
                    // when we don't know if they are pointers, and need
                    // to look into the parent frame.

                    // asserts that only the callee-saves will have
                    // inherited their pointer disposition.
                    assert(spill_reg_idx < NELEMS(frame_map->acfm_spill_reg) &&
                            "too many inherited dispositions");

                    // FIXME: we need to fill this array in order
                    // of least to greatest offset - not the order they
                    // appear on the frame vars list.
                    frame_map->acfm_spill_reg[spill_reg_idx++] = reg_idx;
                    SetBit(frame_map->acfm_spills, bit_to_set);
                    break;
                }
            }
        }
    }
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
        Arena_T frag_arena, temp_state_t* temp_state, ac_frame_t* frame,
        tree_stm_t* body)
{
    // 1. we have to move each incoming register parameter to where it
    // is seen from in the function

    // 2. If we are not doing spilling, then we make room in the frame for
    // all callee-save registers and add instructions here to save them at
    // the start and to restore them at the end.
    // If we are doing spilling, then we save them to and restore them from
    // temporaries.

    // 1. Move register args to temps

    if (frame->acf_arg_moves) {
        body = tree_stm_seq(frame->acf_arg_moves, body);
        frame->acf_arg_moves = NULL; // frame no longer owns this stuff
    }

    // 2. Save callee saves
    const target_t* target = frame->acf_target;
    const size_t num_callee_saves = target->callee_saves.length;
    const size_t word_size = target->word_size;
    temp_t temps_for_callee_saves[num_callee_saves];
    for (int i = 0; i < num_callee_saves; i++) {
        temps_for_callee_saves[i] =
            temp_newtemp(temp_state, word_size, TEMP_DISP_INHERIT);
    }

    tree_stm_t* saves = NULL;
    for (int i = 0; i < num_callee_saves; i++) {
        // TODO: maybe use a type that will be resolved later
        var dst_access = tree_exp_temp(
                temps_for_callee_saves[i], word_size, NULL, frag_arena);
        var src_access = tree_exp_temp(
                target->callee_saves.elems[i], word_size, NULL, frag_arena);
        var move = tree_stm_move(dst_access, src_access);
        saves = (saves) ? tree_stm_seq(saves, move) : move;
    }

    tree_stm_t* restores = NULL;
    for (int i = 0; i < num_callee_saves; i++) {
        var dst_access = tree_exp_temp(
                target->callee_saves.elems[i], word_size, NULL, frag_arena);
        var src_access = tree_exp_temp(
                temps_for_callee_saves[i], word_size, NULL, frag_arena);
        var move = tree_stm_move(dst_access, src_access);
        restores = (restores) ? tree_stm_seq(restores, move) : move;
    }

    body = tree_stm_seq(tree_stm_seq(saves, body), restores);

    return body;
}
