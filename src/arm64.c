#define _GNU_SOURCE // linux: ask for asprintf in stdio.h
#include "arm64.h"
#include <stdio.h> // asprintf
#include <stdlib.h> // abort
#include <string.h> // strlen
#include <inttypes.h> // PRIu64, ...
#include "codegen.h"
#include "fragment.h"
#include "format.h" // fprint_str_escaped
#include "arena_util.h"
#include "assertions.h"

// Useful resources
// - https://developer.arm.com/documentation/102374/0101/Overview?lang=en
// - https://modexp.wordpress.com/2018/10/30/arm64-assembly/

#define var __auto_type
#define Alloc(arena, size) Arena_alloc(arena, size, __FILE__, __LINE__)

#define BitsetLen(len) (((len) + 63) / 64)
#define NELEMS(A) ((sizeof A) / sizeof A[0])

#define fatal(msg) do { perror(msg); abort(); } while(0)

typedef struct codegen_state_t {
    assm_instr_t** ilist;
    temp_state_t* temp_state;
    ac_frame_t* frame;
    sl_fragment_t** ptr_map_fragments;
    Arena_T ret_arena;
    Arena_T frag_arena;
} codegen_state_t;

/*
 * For the common case that there is a codegen_state_t in scope called
 * state, and that we want to return all emitted assm nodes
 */
#define Assm_oper(s, dst, src, jmp) \
    assm_oper(s, dst, src, jmp, state.ret_arena)
#define Assm_label(s, lbl) \
    assm_label(s, lbl, state.ret_arena)
#define Assm_move(s, dst, src) \
    assm_move(s, dst, src, state.ret_arena)

#define ret_alloc(state, size) \
    Arena_alloc(state.ret_arena, size, __FILE__, __LINE__)

#define Asprintf(ret, format, ...) \
    asprintf_arena(state.ret_arena, ret, format, ##__VA_ARGS__)

// pre-declarations
static temp_t munch_exp(codegen_state_t state, tree_exp_t* exp);

static const int word_size = 8;
static const int stack_alignment = 16;

static const temp_t special_regs[] = {
    {.temp_id = 29, .temp_size = word_size}, // fp
    {.temp_id = 30, .temp_size = word_size}, // link register - also caller saved
    {.temp_id = 31, .temp_size = word_size}, // sp - not general purpose
    // On Apple x18 is reserved
    // https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms
    {.temp_id = 18, .temp_size = word_size}
};
#define FP (special_regs[0])
#define SP (special_regs[2])

static const temp_t arm64_argument_regs[] = {
    {.temp_id = 0}, {.temp_id = 1}, {.temp_id = 2}, {.temp_id = 3},
    {.temp_id = 4}, {.temp_id = 5}, {.temp_id = 6}, {.temp_id = 7},
};
static const temp_t arm64_callee_saves[] = {
    {.temp_id = 19, .temp_size = 8},
    {.temp_id = 20, .temp_size = 8},
    {.temp_id = 21, .temp_size = 8},
    {.temp_id = 22, .temp_size = 8},
    {.temp_id = 23, .temp_size = 8},
    {.temp_id = 24, .temp_size = 8},
    {.temp_id = 25, .temp_size = 8},
    {.temp_id = 26, .temp_size = 8},
    {.temp_id = 27, .temp_size = 8},
    {.temp_id = 28, .temp_size = 8},
};

// Means, we would have to save these in order
// for them to survive a a function call
static const temp_t arm64_caller_saves[] = {
    {.temp_id =  8, .temp_size = 8},
    {.temp_id =  9, .temp_size = 8},
    {.temp_id = 10, .temp_size = 8},
    {.temp_id = 11, .temp_size = 8},
    {.temp_id = 12, .temp_size = 8},
    {.temp_id = 13, .temp_size = 8},
    {.temp_id = 14, .temp_size = 8},
    {.temp_id = 15, .temp_size = 8},
    {.temp_id = 16, .temp_size = 8},
    {.temp_id = 17, .temp_size = 8},
    // On Apple x18 is reserved
    // {.temp_id = 18, .temp_size = 8},
};

/*
 * calldefs should contain any registers that a called function
 * will or is allowed to trash
 */
static temp_list_t* calldefs(Arena_T ar)
{
    temp_list_t* c = NULL;

    for (int i = 0; i < NELEMS(arm64_caller_saves); i++) {
        c = temp_list_cons(arm64_caller_saves[i], c, ar);
    }

    // The link register is by convention caller saved
    c = temp_list_cons(special_regs[1], c, ar); //

    // The arguments are allowed to be trashed by the called function
    for (int i = 0; i < NELEMS(arm64_argument_regs); i++) {
        temp_t t = arm64_argument_regs[i];
        t.temp_size = word_size;
        c = temp_list_cons(t, c, ar);
    }

    return c;
}

static void emit(codegen_state_t state, assm_instr_t* new_instr)
{
    new_instr->ai_list = *state.ilist;
    *state.ilist = new_instr;
}

static void emit_ptr_map(codegen_state_t state, ac_frame_map_t* map, sl_sym_t ret_label)
{
    var new_frag = sl_frame_map_fragment(map, ret_label, state.frag_arena);
    new_frag->fr_list = *state.ptr_map_fragments;
    *state.ptr_map_fragments = new_frag;
}

static const char* suff_from_size(size_t size)
{
    switch (size)
    {
        case 8: return "";
        case 4: return "";  // sw for signed word
        case 2: return "h"; // sh for signed half word
        case 1: return "b"; // sb for signed byte
    }
    fprintf(stderr, "invalid size %lu\n", size);
    assert(0 && "invalid size");
}

static const char* suff(tree_exp_t* exp)
{
    return suff_from_size(exp->te_size);
}


// TODO: change the interface so this takes the buffer (or an arena)
static const char* arm64_register_for_size(const char* regname, size_t size)
{
    static char buf[8] = {};
    assert(regname);
    assert(strlen(regname) >= 2 && strlen(regname) <= 3);
    assert(strcmp(regname, "sp")==0
            || strcmp(regname, "fp")==0
            || regname[0] == 'x');
    assert(size == 8 || size == 4 || size == 2 || size == 1);

    strcpy(buf, regname);
    if (size == 8) {
        return buf;
    } else if (buf[0] == 'x') {
        buf[0] = 'w';
    }
    return buf;
}

static assm_instr_t*
arm64_load_temp(struct ac_frame_var* v, temp_t temp, Arena_T ar)
{
    char* s = NULL;
    asprintf_arena(ar, &s, "ldr%s	`d0, [`s0, #%d]	; unspill\n",
            suff_from_size(v->acf_size), v->acf_offset);
    var src_list = temp_list(FP, ar);
    return assm_oper(s, temp_list(temp, ar), src_list, NULL, ar);
}

static assm_instr_t*
arm64_store_temp(struct ac_frame_var* v, temp_t temp, Arena_T ar)
{
    char* s = NULL;
    asprintf_arena(ar, &s, "str%s	`s0, [`s1, #%d]	; spill\n",
            suff_from_size(v->acf_size), v->acf_offset);
    var src_list =
        temp_list_cons(temp,
                temp_list(FP, ar), ar);
    return assm_oper(
            s,
            NULL, /* dst list */
            src_list,
            NULL, /* jump=None */
            ar);
}


/*
 * Immediates in instructions can be shifted 16-bit values.
 * We say whether they meet the minimum requirements in the sense that
 * it fit, unshifted, in 16 bits.
 * I think to work out if if could be a shifted 16 bits, we would
 * look at the number of trailing zeros and then if they could be removed
 * by shifting and if that would then be 16 bits.... but let's hold off for now
 */
static bool can_be_immediate(int constant_value) __attribute__((const));
static bool can_be_immediate(int constant_value)
{
    return (constant_value >= INT16_MIN &&
            constant_value <= INT16_MAX);
}

/*
 * Used mostly for working out the total size required when considering
 * the alignment requirements of adjacent stored data.
 */
static int round_up_size(int size, int multiple) __attribute__((const));
static int round_up_size(int size, int multiple)
{
    return ((size + multiple - 1) / multiple) * multiple;
}

static temp_list_t* munch_stack_args(codegen_state_t state, tree_exp_t* exp)
{
#define temp_list(t) temp_list(t, state.ret_arena)
#define temp_list_cons(h, t) temp_list_cons(h, t, state.ret_arena)
    // This will be wrong / broken!

    for (var e = exp; e; e = e->te_list) {
        assert(e->te_size <= word_size && "TODO: larger stack args");
    }

    size_t total_size = 0;
    for (var e = exp; e; e = e->te_list) {
        var src = munch_exp(state, e);
        char* s = NULL;
        Asprintf(&s, "str%s  `s0, [`s1, #%zu]\n",
                suff(e), total_size);
        var src_list =
            temp_list_cons(src,
                    temp_list(SP));
        emit(state, Assm_oper(s, NULL, src_list, NULL));


        size_t field_alignment =
            /* use size as alignment for types smaller than 8 bytes */
            e->te_size;
        total_size = round_up_size(total_size, field_alignment);
        total_size += e->te_size;
    }
    total_size = round_up_size(total_size, stack_alignment);

    reserve_outgoing_arg_space(state.frame, total_size);

    return NULL;
#undef temp_list
#undef temp_list_cons
}

static temp_list_t* munch_args(codegen_state_t state, int arg_idx, tree_exp_t* exp)
{
#define temp_list(t) temp_list(t, state.ret_arena)
#define temp_list_cons(h, t) temp_list_cons(h, t, state.ret_arena)
    if (exp == NULL) {
        // no more remaining arguments, return empty list
        return NULL;
    }

    if (arg_idx < NELEMS(arm64_argument_regs)) {
        var param_reg = arm64_argument_regs[arg_idx];

        if (exp->te_size <= 8) {
            // TODO: it might be better to munch remaining args before
            // this one so that there are more free registers when moving the
            // later args into the stack - if need be...
            param_reg.temp_size = exp->te_size;
            var src = munch_exp(state, exp);
            char* s = NULL;
            Asprintf(&s, "mov	`d0, `s0\n");
            emit(state, Assm_move(s, param_reg, src));

            return temp_list_cons(
                    param_reg,
                    munch_args(state, arg_idx + 1, exp->te_list)
                    );
        } else if (exp->te_size <= 16) {
            param_reg.temp_size = word_size;
            assert(arg_idx + 1 < NELEMS(arm64_argument_regs));
            var param_reg2 = arm64_argument_regs[arg_idx + 1];
            param_reg2.temp_size = exp->te_size - word_size;

            munch_exp(state, exp);
            // TODO: change munch to return a list of temps
            assert(!"TODO: larger arguments");

            return temp_list_cons(
                    param_reg,
                    temp_list_cons(
                        param_reg2,
                        munch_args(state, arg_idx + 2, exp->te_list)));
        } else {
            assert(!"go away large arguments");
        }
    } else {
        /*
         * Remaining Arguments must be passed on the stack
         */
        return munch_stack_args(state, exp);
    }
#undef temp_list
#undef temp_list_cons
}

/*
 * creates a new temporary for storing the result of the expression
 */
static temp_t new_temp_for_exp(temp_state_t* temp_state, tree_exp_t* exp)
{
    return temp_newtemp(temp_state, exp->te_size,
            tree_dispo_from_type(exp->te_type));
}

static temp_t munch_exp(codegen_state_t state, tree_exp_t* exp)
{
#define Munch_exp(_exp) munch_exp(state, _exp)
#define temp_list(t) temp_list(t, state.ret_arena)
#define temp_list_cons(h, t) temp_list_cons(h, t, state.ret_arena)

    switch (exp->te_tag) {
        case TREE_EXP_MEM:
        {
            var addr = exp->te_mem_addr;
            // MEM(BINOP(+, e1, CONST))
            if (addr->te_tag == TREE_EXP_BINOP
                    && addr->te_binop == TREE_BINOP_PLUS) {
                if (addr->te_rhs->te_tag == TREE_EXP_CONST) {
                    var r = new_temp_for_exp(state.temp_state, exp);
                    char* s = NULL;
                    Asprintf(&s, "ldr%s	`d0, [`s0, #%d]\n",
                            suff(exp), addr->te_rhs->te_const);
                    var src_list = temp_list(Munch_exp(addr->te_lhs));
                    emit(state,
                        Assm_oper(s, temp_list(r), src_list, NULL));
                    return r;
                }
                // we could emit something if some of the other cases come
                // up
                tree_printf(stderr, "$$$ %E\n", exp);
            }

            // MEM(e1)
            var r = new_temp_for_exp(state.temp_state, exp);
            char* s = NULL;
            Asprintf(&s, "ldr%s	`d0, [`s0]\n", suff(exp));
            var src_list = temp_list(Munch_exp(addr));
            emit(state,
                 Assm_oper(s, temp_list(r), src_list, NULL));
            return r;
        }
        case TREE_EXP_BINOP:
        {
            // BINOP(+, e1, CONST)
            if (exp->te_binop == TREE_BINOP_PLUS
                    && exp->te_rhs->te_tag == TREE_EXP_CONST) {
                if (can_be_immediate(exp->te_rhs->te_const)) {
                    temp_t r = new_temp_for_exp(state.temp_state, exp);
                    char* s = NULL;
                    Asprintf(&s, "add	`d0, `s0, #%d\n",
                            exp->te_rhs->te_const);
                    var src_list = temp_list(Munch_exp(exp->te_lhs));
                    emit(state,
                            Assm_oper(s, temp_list(r), src_list, NULL));
                    return r;
                }
                tree_printf(stderr, "$$$ %E\n", exp);
            }

            // BINOP(+, e1, e2)
            const char* op;
            switch (exp->te_binop) {
                case TREE_BINOP_PLUS: op = "add"; break;
                case TREE_BINOP_MINUS: op = "sub"; break;
                case TREE_BINOP_MUL: op = "mul"; break;
                case TREE_BINOP_DIV: op = "sdiv"; break;
                case TREE_BINOP_AND: op = "and"; break;
                case TREE_BINOP_OR: op = "orr"; break;
                case TREE_BINOP_XOR: op = "eor"; break;
                case TREE_BINOP_LSHIFT: op = "lsl"; break;
                case TREE_BINOP_RSHIFT: op = "lsr"; break;
                case TREE_BINOP_ARSHIFT: op = "asr"; break;
            }
            temp_t r = new_temp_for_exp(state.temp_state, exp);
            char* s = NULL;
            Asprintf(&s, "%s	`d0, `s0, `s1\n", op);
            var src_list =
                temp_list_cons(Munch_exp(exp->te_lhs),
                        temp_list(Munch_exp(exp->te_rhs)));
            emit(state,
                    Assm_oper(s, temp_list(r), src_list, NULL));
            return r;
        }
        case TREE_EXP_CONST:
        {
            assert(exp->te_size <= 8);
            temp_t r = new_temp_for_exp(state.temp_state, exp);
            char* s = NULL;
            if (can_be_immediate(exp->te_const)) {
                Asprintf(&s, "mov	`d0, #%d\n", exp->te_const);
            } else {
                Asprintf(&s, "ldr	`d0, =%d\n", exp->te_const);
            }
            emit(state, Assm_oper(s, temp_list(r), NULL, NULL));
            return r;
        }
        case TREE_EXP_TEMP:
        {
            assert(exp->te_size <= 8);
            return exp->te_temp;
        }
        case TREE_EXP_NAME:
        {
            /*
             * A label pointing to some data (or maybe a function in the future)
             */
            temp_t r = new_temp_for_exp(state.temp_state, exp);
            {
                char* s = NULL;
                Asprintf(&s, "adrp	`d0, %s@PAGE\n", exp->te_name);
                emit(state, Assm_oper(s, temp_list(r), NULL, NULL));
            }
            {
                char* s = NULL;
                Asprintf(&s, "add	`d0, `s0, %s@PAGEOFF\n", exp->te_name);
                emit(state, Assm_oper(s, temp_list(r), temp_list(r), NULL));
            }
            return r;
        }
        case TREE_EXP_CALL:
        {
            assert(exp->te_size <= 8 && "TODO larger sizes");
            var func = exp->te_func;
            var args = exp->te_args;
            if (func->te_tag == TREE_EXP_NAME) {
                char* s = NULL;
                Asprintf(&s, "bl	_%s\n", func->te_name);
                emit(state, Assm_oper(
                            s, calldefs(state.ret_arena),
                            munch_args(state, 0, args), NULL));
            } else {
                tree_printf(stderr, ">>> %E\n", exp);
                assert(!"TODO: TREE_EXP_CALL");
            }

            /*
             * Here we a insert label directly after the call instruction
             * to use as a key for the stack map ...
             * i.e. the label refers to the return address the for the
             * function that is being called.
             */

            // I guess we can find this instruction again by looking
            // for the label and then looking one instruction before.
            sl_sym_t retaddr_label = temp_prefixedlabel(state.temp_state, "ret");
            char* s = NULL;
            Asprintf(&s, "%s:\n", retaddr_label);
            emit(state, Assm_label(s, retaddr_label));
            emit_ptr_map(state, exp->te_ptr_map, retaddr_label);

            // And the result of the call will be in the first result register
            temp_t r = state.frame->acf_target->tgt_ret0;
            r.temp_size = exp->te_size;
            assert(r.temp_size);
            return r;
        }
        case TREE_EXP_ESEQ:
        {
            assert(!"eseqs should no longer exist");
        }
    }
#undef temp_list
#undef temp_list_cons
#undef Munch_exp
}

static void debug_print_drop(tree_stm_t* stm)
{
    #ifndef NDEBUG
        tree_printf(stderr, "dropping dead code: %S\n", stm);
    #endif
}

static void munch_stm(codegen_state_t state, tree_stm_t* stm)
{
#define Munch_stm(_stm) munch_stm(state, _stm)
#define Munch_exp(_exp) munch_exp(state, _exp)
#define temp_list(t) temp_list(t, state.ret_arena)
#define temp_list_cons(h, t) temp_list_cons(h, t, state.ret_arena)

    switch (stm->tst_tag) {
        case TREE_STM_SEQ:
        {
            Munch_stm(stm->tst_seq_s1);
            Munch_stm(stm->tst_seq_s2);
            break;
        }
        case TREE_STM_MOVE:
        {
            // TODO: think about when this might need to be a signed
            // load...

            var src = stm->tst_move_exp;
            var dst = stm->tst_move_dst;
            // ## store
            if (dst->te_tag == TREE_EXP_MEM) {
                // TODO: all the various offset variations
                var addr = dst->te_mem_addr;
                if (addr->te_tag == TREE_EXP_BINOP
                        && addr->te_binop == TREE_BINOP_PLUS) {
                    if (addr->te_rhs->te_tag == TREE_EXP_CONST) {
                        char* s = NULL;
                        Asprintf(&s, "str%s	`s0, [`s1, #%d]\n",
                                suff(src), addr->te_rhs->te_const);
                        var src_list =
                            temp_list_cons(Munch_exp(src),
                                temp_list(Munch_exp(addr->te_lhs)));
                        emit(state,
                            Assm_oper(s, NULL, src_list, NULL));
                        return;
                    }
                    tree_printf(stderr, "$$$ %S\n", stm);
                }

                char* s = NULL;
                Asprintf(&s, "str%s	`s0, [`s1]\n", suff(src));

                var src_list =
                    temp_list_cons(Munch_exp(src),
                            temp_list(Munch_exp(addr)));
                emit(state,
                        Assm_oper(s,
                            NULL, src_list, NULL));
            } else if (dst->te_tag == TREE_EXP_TEMP) {
                // temp is already handled here, and call is
                // not possible to optimize

                temp_t src_t = Munch_exp(src);
                if (src_t.temp_size == 0 || dst->te_temp.temp_size == 0) {
                    // Omit size 0 move.
                    debug_print_drop(stm);
                    break;
                }
                assert(src_t.temp_size == dst->te_temp.temp_size);
                char* s = NULL;
                Asprintf(&s, "mov	`d0, `s0\n");
                emit(state,
                        Assm_move(s,
                            dst->te_temp,
                            src_t));
            } else {
                tree_printf(stderr, ">>> %S\n", stm);
                assert(0 && "move into neither memory or register");
            }
            break;
        }
        case TREE_STM_LABEL:
        {
            char* s = NULL;
            Asprintf(&s, "%s:\n", stm->tst_label);
            emit(state, Assm_label(s, stm->tst_label));
            break;
        }
        case TREE_STM_EXP:
        {
            if (stm->tst_exp->te_tag != TREE_EXP_CALL) {
                debug_print_drop(stm);
                break;
            }

            assert(stm->tst_exp->te_size <= word_size);
            var t = new_temp_for_exp(state.temp_state, stm->tst_exp);
            // Move the result to an unused temporary so that the result
            // registers don't stay live for the rest of the function
            var r = Munch_exp(stm->tst_exp);
            char* s = NULL;
            Asprintf(&s, "mov	`d0, `s0\n");
            emit(state, Assm_move(s, t, r));
            break;
        }
        case TREE_STM_CJUMP:
        {
            // CJUMP(op, 0, e2, Ltrue, Lfalse)
            if (stm->tst_cjump_lhs->te_tag == TREE_EXP_CONST
                    && stm->tst_cjump_lhs->te_const == 0) {
                if (stm->tst_cjump_op == TREE_RELOP_EQ
                        || stm->tst_cjump_op == TREE_RELOP_NE) {
                    var src_list =
                        temp_list(Munch_exp(stm->tst_cjump_rhs));

#define COMMON \
                    sl_sym_t* jump = ret_alloc(state, 3 * sizeof *jump); \
                    jump[0] = stm->tst_cjump_true; \
                    jump[1] = stm->tst_cjump_false; \
 \
                    char* s = NULL; \
                    if (stm->tst_cjump_op == TREE_RELOP_EQ) { \
                        Asprintf(&s, "cbz	`s0, %s\n", stm->tst_cjump_true); \
                    } else { \
                        assert(stm->tst_cjump_op == TREE_RELOP_NE); \
                        Asprintf(&s, "cbnz	`s0, %s\n", stm->tst_cjump_true); \
                    } \
                    emit(state, Assm_oper(s, NULL, src_list, jump));
                    COMMON
                    return;
                }
            }
            // CJUMP(op, e1, 0, Ltrue, Lfalse)
            if (stm->tst_cjump_rhs->te_tag == TREE_EXP_CONST
                    && stm->tst_cjump_rhs->te_const == 0) {
                // TODO: factor this and the code above properly
                if (stm->tst_cjump_op == TREE_RELOP_EQ
                        || stm->tst_cjump_op == TREE_RELOP_NE) {
                    var src_list =
                        temp_list(Munch_exp(stm->tst_cjump_lhs));

                    COMMON
#undef COMMON
                    return;
                }
            }

            // CJUMP(op, e1, e2, Ltrue, Lfalse)
            {
                char* s = NULL;
                Asprintf(&s, "cmp	`s0, `s1\n");
                var src_list = temp_list_cons(
                        Munch_exp(stm->tst_cjump_lhs),
                        temp_list(Munch_exp(stm->tst_cjump_rhs)));
                emit(state, Assm_oper(s, NULL, src_list, NULL));
            }

            sl_sym_t* jump = ret_alloc(state, 3 * sizeof *jump);
            jump[0] = stm->tst_cjump_true;
            jump[1] = stm->tst_cjump_false;

            // I think for the missing ones, it would be possible
            // to use CMN instead of CMP ...
            const char* op;
            switch (stm->tst_cjump_op) {
                case TREE_RELOP_EQ: op = "b.eq"; break;
                case TREE_RELOP_NE: op = "b.ne"; break;
                case TREE_RELOP_GT: op = "b.gt"; break;
                case TREE_RELOP_GE: op = "b.ge"; break;
                case TREE_RELOP_LT: op = "b.lt"; break;
                case TREE_RELOP_LE: op = "b.le"; break;
                case TREE_RELOP_ULT: assert(!"TREE_RELOP_ULT"); break;
                case TREE_RELOP_ULE: op = "b.ls"; break; // lower or same
                case TREE_RELOP_UGT: op = "b.hi"; break; // higher
                case TREE_RELOP_UGE: assert(!"TREE_RELOP_UGE"); break;
            }
            char* s = NULL;
            Asprintf(&s, "%s	%s\n", op, stm->tst_cjump_true);
            emit(state, Assm_oper(s, NULL, NULL, jump));
            break;
        }
        case TREE_STM_JUMP:
        {
            // b	Lblah
            if (stm->tst_jump_num_labels == 1) {
                char* s = NULL;
                Asprintf(&s, "b	%s\n", stm->tst_jump_labels[0]);
                sl_sym_t* jump = ret_alloc(state, 2 * sizeof *jump);
                jump[0] = stm->tst_jump_labels[0];
                emit(state, Assm_oper(s, NULL, NULL, jump));
            } else {
                tree_printf(stderr, ">>> %S\n", stm);
                assert(0 && "TODO: switch");
            }
            break;
        }
    }
#undef temp_list
#undef temp_list_cons
#undef Munch_exp
#undef Munch_stm
}

/*
 * arm64_codegen selects instructions for a single statement in the tree IR
 * language. It returns a list of instructions in the arm64 architecture
 */
assm_instr_t* /* list */
arm64_codegen(
        Arena_T arena, Arena_T frag_arena, temp_state_t* temp_state,
        sl_fragment_t* fragment, tree_stm_t* stm)
{
    assm_instr_t* result = NULL;
    sl_fragment_t* ptr_map_fragments = NULL;
    codegen_state_t codegen_state = {
        .ilist = &result,
        .temp_state = temp_state,
        .frame = fragment->fr_frame,
        .ptr_map_fragments = &ptr_map_fragments,
        .ret_arena = arena,
        .frag_arena = frag_arena,
    };
    munch_stm(codegen_state, stm);

    /*
     * insert the generated frame maps into the list of all fragments
     */
    fragment->fr_list = fr_append(ptr_map_fragments, fragment->fr_list);

    return assm_list_reverse(result);
}

/*
 * proc_entry_exit_2 defines which temporaries i.e. registers are live-out
 * of the function
 */
static assm_instr_t*
arm64_proc_entry_exit_2(ac_frame_t* frame, assm_instr_t* body, Arena_T ar)
{
    temp_list_t* src_list = NULL;
    for (int i = 0; i < NELEMS(arm64_callee_saves); i++) {
        src_list = temp_list_cons(arm64_callee_saves[i], src_list, ar);
    }
    src_list = temp_list_cons(FP, src_list, ar);
    src_list = temp_list_cons(SP, src_list, ar);
    src_list = temp_list_cons(special_regs[3], src_list, ar); // x18 - apple
                                                          // reserved

    temp_t ret0 = frame->acf_target->tgt_ret0;
    ret0.temp_size = word_size;
    src_list = temp_list_cons(ret0, src_list, ar); // x0
    if (0) { // TODO: check larger return sizes
        temp_t ret1 = frame->acf_target->tgt_ret1;
        ret1.temp_size = word_size;
        src_list = temp_list_cons(ret1, src_list, ar); // x1
    }

    // A jump list end with a terminating null.
    // So this is just the terminating null only.
    sl_sym_t* empty_jump_list = Alloc(ar, sizeof *empty_jump_list);

    var sink_instr = assm_oper(
            strdup_arena(ar, "\n"),
            NULL,
            src_list,
            empty_jump_list,
            ar);

    // append sink instruction to body
    var b = body;
    while (b->ai_list) {
        b = b->ai_list;
    }
    b->ai_list = sink_instr;

    return body;
}

static assm_fragment_t
arm64_proc_entry_exit_3(ac_frame_t* frame, assm_instr_t* body, Arena_T ar)
{
    const char* fn_label = frame->acf_name;
    int frame_size = ac_frame_words(frame)*word_size;
    char* prologue = NULL;
    asprintf_arena(ar, &prologue, "\
	.globl	_%s\n\
	.p2align	2\n\
_%s:\n\
	.cfi_startproc\n\
	stp	x29, x30, [sp, #-16]!\n\
	mov	fp, sp\n\
	.cfi_def_cfa w29, 16\n\
	.cfi_offset w30, -8\n\
	.cfi_offset w29, -16\n\
	sub	sp, sp, #%d\n\
",
            fn_label, fn_label, frame_size);

    char* epilogue = NULL;
    asprintf_arena(ar, &epilogue, "\
	add	sp, sp, #%d\n\
	ldp	x29, x30, [sp], #16\n\
	ret\n\
	.cfi_endproc\n\
",
            frame_size);

    return (assm_fragment_t){
        .asf_prologue = prologue,
        .asf_instrs = body,
        .asf_epilogue = epilogue,
    };
}

static void
emit_text_segment_header(FILE* out)
{
    fprintf(out, "\t.section	__TEXT,__text,regular,pure_instructions\n");
}

static int
get_callee_save_idx(int reg_idx)
{
    int i = 0;
    for (; i < NELEMS(arm64_callee_saves); i++) {
        if (arm64_callee_saves[i].temp_id == reg_idx) {
            break;
        }
    }
    return i;
}

static void
emit_frame_map_entry(
        FILE* out, const sl_fragment_t* frag, Table_T label_to_cs_bitmap,
        int entry_num)
{

    fprintf(out, "	.p2align	3\n");
    fprintf(out, "Lptrmap%d:\n", entry_num);

    // The pointer to the previous frame map
    if (entry_num == 0) {
        fprintf(out, "	.quad	0\n");
    } else {
        fprintf(out, "	.quad	Lptrmap%d\n", entry_num - 1);
    }

    fprintf(out, "	.quad	%s	; return address - the key\n",
            frag->fr_ret_label);

    union { uint32_t bm; void* v; } cs_bitmap = {};
    static_assert(sizeof(cs_bitmap.v) >= sizeof(cs_bitmap.bm),
            "16-bit machine?");
    cs_bitmap.v = Table_get(label_to_cs_bitmap, frag->fr_ret_label);
    fprintf(out, "	.long	%"PRIu32"	; callee-save bitmap\n",
            cs_bitmap.bm);

    var map = frag->fr_map;

    if (map->acfm_num_arg_words > UINT16_MAX) {
        fatal("num arg words > uint16_max");
    }
    if (map->acfm_num_local_words > UINT16_MAX) {
        fatal("num local words > uint16_max");
    }
    assert(map->acfm_num_spill_words <= map->acfm_num_local_words);

    // This number actually includes the saved FP and RA...
    fprintf(out, "	.short	%d	; number of stack args + 2\n",
            map->acfm_num_arg_words);

    // This number also includes the length of the spill space
    fprintf(out, "	.short	%d	; length of locals space\n",
            map->acfm_num_local_words);

    fprintf(out, "	.short	%d	; length of spills space\n",
            map->acfm_num_spill_words);

    // frame_map->acfm_spill_reg contains indexes into arm64_registers
    // Take each, and turn it into a 4-bit index into arm64_callee_saves
    // stuff those values into the next 40 bits.
    uint8_t spill_reg[5] = {};
    for (int i = 0; i < 10; (i+=2)) {
        uint8_t cs_idxs[2] = {
            get_callee_save_idx(map->acfm_spill_reg[i + 0]),
            get_callee_save_idx(map->acfm_spill_reg[i + 1]),
        };
        spill_reg[(i>>1)] = cs_idxs[0] | (cs_idxs[1] << 4);
    }

    for (int i = 0; i < 5; i++) {
        fprintf(out, "	.byte	%"PRIu8"	; spill_reg\n",
                spill_reg[i]);
    }
    fprintf(out, "	.zero	%d\n", 1); // padding

    for (int i = 0; i < BitsetLen(map->acfm_num_arg_words); i++) {
        fprintf(out, "	.quad	%"PRIu64"	; arg bitmap\n",
                map->acfm_args[i]);
    }
    for (int i = 0; i < BitsetLen(map->acfm_num_local_words); i++) {
        fprintf(out, "	.quad	%"PRIu64"	; locals bitmap\n",
                map->acfm_locals[i]);
    }
    for (int i = 0; i < BitsetLen(map->acfm_num_spill_words); i++) {
        fprintf(out, "	.quad	%"PRIu64"	; spills bitmap\n",
                map->acfm_spills[i]);
    }
}

static void
emit_frame_map_entry_root(
        FILE* out, int entry_num)
{
    // point to the final entry
    fprintf(out, "\
	.globl	_sl_rt_frame_maps\n\
	.p2align	3\n\
_sl_rt_frame_maps:\n\
	.quad	Lptrmap%d\n\
",
        entry_num - 1);
}

static void
emit_data_segment(
        FILE* out, const sl_fragment_t* fragments, Table_T label_to_cs_bitmap)
{
    fprintf(out, "\n");
    fprintf(out, "\t.section	__TEXT,__cstring,cstring_literals\n");

    for (var frag = fragments; frag; frag = frag->fr_list) {
        switch (frag->fr_tag) {
            case FR_CODE:
                continue;
            case FR_STRING:
            {
                fprintf(out, "%s:\n", frag->fr_label);

                size_t required = fmt_escaped_len(frag->fr_string);
                assert(required < 512);
                char buf[512];
                fmt_snprint_escaped(buf, 512, frag->fr_string);
                fprintf(out, "	.asciz	%s\n", buf);
                break;
            }
            case FR_FRAME_MAP:
                continue;
        }
    }

    fprintf(out, "\n");
    fprintf(out, "\t.section	__DATA,__const\n");

    int entry_num = 0;
    for (var frag = fragments; frag; frag = frag->fr_list) {
        switch (frag->fr_tag) {
            case FR_CODE:
                continue;
            case FR_STRING:
                continue;
            case FR_FRAME_MAP:
                emit_frame_map_entry(
                        out, frag, label_to_cs_bitmap, entry_num);
                entry_num = entry_num + 1;
                break;
        }
    }

    if (entry_num > 0) {
        emit_frame_map_entry_root(out, entry_num);
    }
}

/*
 *
 */

// There is also an xzr/wzr that always contains zero...
const char* arm64_registers[] = {
    "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
    "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
    "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
    "x24", "x25", "x26", "x27", "x28", "fp", "x30" , "sp",
};
const temp_t arm64_temp_map_temps[] = {
    {.temp_id = 0}, {.temp_id = 1}, {.temp_id = 2}, {.temp_id = 3},
    {.temp_id = 4}, {.temp_id = 5}, {.temp_id = 6}, {.temp_id = 7},
    {.temp_id = 8}, {.temp_id = 9}, {.temp_id = 10}, {.temp_id = 11},
    {.temp_id = 12}, {.temp_id = 13}, {.temp_id = 14}, {.temp_id = 15},
    {.temp_id = 16}, {.temp_id = 17}, {.temp_id = 18}, {.temp_id = 19},
    {.temp_id = 20}, {.temp_id = 21}, {.temp_id = 22}, {.temp_id = 23},
    {.temp_id = 24}, {.temp_id = 25}, {.temp_id = 26}, {.temp_id = 27},
    {.temp_id = 28}, {.temp_id = 29}, {.temp_id = 30}, {.temp_id = 31}
};

static codegen_t arm64_codegen_module = {
    .codegen = arm64_codegen,
    .proc_entry_exit_2 = arm64_proc_entry_exit_2,
    .proc_entry_exit_3 = arm64_proc_entry_exit_3,
    .load_temp = arm64_load_temp,
    .store_temp = arm64_store_temp,
    .emit_text_segment_header = emit_text_segment_header,
    .emit_data_segment = emit_data_segment,
};

static Table_T arm64_temp_map();

const target_t target_arm64 = {
    .word_size = 8,
    .stack_alignment = 16,
    .arg_registers = {
        .length = NELEMS(arm64_argument_regs),
        .elems = arm64_argument_regs,
    },
    .tgt_sp = {.temp_id = 31, .temp_size = 8}, // sp - not general purpose
    .tgt_fp = {.temp_id = 29, .temp_size = 8}, // fp
    .tgt_ret0 = {.temp_id = 0},
    .tgt_ret1 = {.temp_id = 1},
    .callee_saves = {
        .length = NELEMS(arm64_callee_saves),
        .elems = arm64_callee_saves,
    },
    .register_names = arm64_registers,
    .register_for_size = arm64_register_for_size,
    .tgt_temp_map = arm64_temp_map,
    .tgt_backend = &arm64_codegen_module,
};
static_assert(NELEMS(arm64_callee_saves) <= TARGET_CS_COUNT_MAX,
        "callee save count");

/*
 * These are used when we have Tables with temp_t's as keys
 */
static int cmptemp(const void* x, const void* y)
{
    const temp_t* xx = x;
    const temp_t* yy = y;
    return xx->temp_id - yy->temp_id;
}
static unsigned hashtemp(const void* key)
{
    const temp_t* k = key;
    return k->temp_id;
}


static Table_T arm64_temp_map()
{
    static Table_T result = NULL;
    if (result)
        return result;

    result = Table_new(0, cmptemp, hashtemp);
    for (int i = 0; i < NELEMS(arm64_registers); i++) {
        Table_put(result,
                &(arm64_temp_map_temps[i]),
                (void*)arm64_registers[i]);
    }
    return result;
}
