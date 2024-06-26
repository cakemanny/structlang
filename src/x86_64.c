#define _GNU_SOURCE // linux: ask for asprintf in stdio.h
#include "x86_64.h"
#include <stdlib.h>
#include <string.h> // strdup
#include "format.h" // fprint_str_escaped
#include "arena_util.h"
#include "assertions.h"

// Useful references
// - https://web.stanford.edu/class/cs107/guide/x86-64.html

#define var __auto_type
#define Alloc(arena, size) Arena_alloc(arena, size, __FILE__, __LINE__)

#define NELEMS(A) ((sizeof A) / sizeof A[0])

typedef struct codegen_state_t {
    assm_instr_t** ilist;
    temp_state_t* temp_state;
    ac_frame_t* frame;
    /* */
    Arena_T ret_arena; // for assm instructions
    Arena_T frag_arena; // for fragments, frame stuff
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
    {.temp_id = 0, .temp_size = word_size}, // rax  return value pt1
    {.temp_id = 2, .temp_size = word_size}, // rdx  return value pt2
    {.temp_id = 4, .temp_size = word_size}, // rsp  stack pointer
    {.temp_id = 5, .temp_size = word_size}, // rbp  frame pointer
};
#define FP (special_regs[3])
#define SP (special_regs[2])

// is this what he meant by outgoing arguments?
static const temp_t argregs[] = {
    {.temp_id = 7}, // rdi
    {.temp_id = 6}, // rsi
    {.temp_id = 2}, // rdx
    {.temp_id = 1}, // rcx
    {.temp_id = 8}, // r8
    {.temp_id = 9}, // r9
};

// rbp is not included since it's a special reg (the frame pointer)
// registers that won't be trashed by a called function
static const temp_t callee_saves[] = {
    {.temp_id = 3 , .temp_size = word_size}, /* rbx */
    {.temp_id = 12, .temp_size = word_size}, /* r12 */
    {.temp_id = 13, .temp_size = word_size}, /* r13 */
    {.temp_id = 14, .temp_size = word_size}, /* r14 */
    {.temp_id = 15, .temp_size = word_size}, /* r15 */
};

// things we trash
static const temp_t caller_saves[] = {
    {.temp_id = 10, .temp_size = word_size}, // r10  could be used for static chain pointer - if wanted
    {.temp_id = 11, .temp_size = word_size}, // r11
};


static const char* registers_8bit[] = {
    "%al", "%cl", "%dl", "%bl", "%spl", "%bpl", "%sil", "%dil",
    "%r8b", "%r9b", "%r10b", "%r11b", "%r12b", "%r13b", "%r14b", "%r15b"
};
static const char* registers_16bit[] = {
    "%ax", "%cx", "%dx", "%bx", "%sp", "%bp", "%si", "%di",
    "%r8w", "%r9w", "%r10w", "%r11w", "%r12w", "%r13w", "%r14w", "%r15w"
};
static const char* registers_32bit[] = {
    "%eax", "%ecx", "%edx", "%ebx", "%esp", "%ebp", "%esi", "%edi",
    "%r8d", "%r9d", "%r10d", "%r11d", "%r12d", "%r13d", "%r14d", "%r15d"
};
static const char* registers_64bit[] = {
    "%rax", "%rcx", "%rdx", "%rbx", "%rsp", "%rbp", "%rsi", "%rdi",
    "%r8", "%r9", "%r10", "%r11", "%r12", "%r13", "%r14", "%r15"
};


/*
 * calldefs should contain any registers that a called function
 * will or is allowed to trash
 */
static temp_list_t* calldefs(Arena_T ar)
{
    temp_list_t* c = NULL;

    for (int i = 0; i < NELEMS(caller_saves); i++) {
        c = temp_list_cons(caller_saves[i], c, ar);
    }

    // the return value registers
    c = temp_list_cons(special_regs[0], c, ar); // rax
    // -- the next line should be commented if we include it already in args
    // c = temp_list_cons(special_regs[1], c); // rdx

    for (int i = 0; i < NELEMS(argregs); i++) {
        temp_t t = argregs[i];
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

/**
 * return the at&t syntax instruction suffix that needs to be used for
 * the expression
 */
static const char* suff_from_size(size_t size)
{
    switch (size)
    {
        case 8: return "q";
        case 4: return "l";
        case 2: return "w";
        case 1: return "b";
    }
    fprintf(stderr, "invalid size %lu\n", size);
    assert(0 && "invalid size");
}

static const char* suff(tree_exp_t* exp)
{
    return suff_from_size(exp->te_size);
}


static const char* x86_64_register_for_size(const char* regname, size_t size)
{
    assert(regname);
    assert(strlen(regname) >= 2 && strlen(regname) <= 3);
    assert(regname[0] == 'r');
    assert(size == 8 || size == 4 || size == 2 || size == 1);

    const char** possible_registers[] = {
        registers_8bit, registers_16bit, registers_32bit, registers_64bit
    };
    const char** registers = possible_registers[__builtin_ctzll(size)];

    switch (regname[1]) {
        // rax
        case 'a': return registers[0];
        // rbx
        case 'b': {
                      if (regname[2] == 'x') return registers[3]; // rbx
                      assert(regname[2] == 'p'); return registers[5]; // rbp
                  }
        case 'c': return registers[1]; // rcx
        case 'd': {
                      if (regname[2] == 'x') return registers[2]; // rdx
                      assert(regname[2] == 'i'); return registers[7]; // rdi
                  }
        case 's': {
                      if (regname[2] == 'p') return registers[4]; // rsp
                      assert(regname[2] == 'i'); return registers[6]; // rsi
                  }
        case '8': return registers[8];
        case '9': return registers[9];
        case '1':
        {
            switch (regname[2]) {
                case '0': return registers[10];
                case '1': return registers[11];
                case '2': return registers[12];
                case '3': return registers[13];
                case '4': return registers[14];
                case '5': return registers[15];
            }
        }
    }

    fprintf(stderr, "unexpected register name %s\n", regname);
    assert(!"unexpected register name");
}

static assm_instr_t*
x86_64_load_temp(struct ac_frame_var* v, temp_t temp, Arena_T ar)
{
    char* s = NULL;
    asprintf_arena(ar, &s, "mov%s %d(`s0), `d0	# unspill\n",
            suff_from_size(v->acf_size), v->acf_offset);
    var src_list = temp_list(FP, ar);
    return assm_oper(s, temp_list(temp, ar), src_list, NULL, ar);
}

static assm_instr_t*
x86_64_store_temp(struct ac_frame_var* v, temp_t temp, Arena_T ar)
{
    char* s = NULL;
    asprintf_arena(ar, &s, "mov%s `s1, %d(`s0)	# spill\n",
            suff_from_size(v->acf_size), v->acf_offset);
    var src_list =
        temp_list_cons(FP,
                temp_list(temp, ar), ar);
    return assm_oper(
            s,
            NULL, /* dst list */
            src_list,
            NULL, /* jump=None */
            ar);
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
        Asprintf(&s, "mov%s  `s1, %zu(`s0)\n",
                suff(e), total_size);
        var src_list =
            temp_list_cons(SP,
                    temp_list(src));
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

    if (arg_idx < NELEMS(argregs)) {
        var param_reg = argregs[arg_idx];

        if (exp->te_size <= 8) {
            param_reg.temp_size = exp->te_size;
            var src = munch_exp(state, exp);
            char* s = NULL;
            Asprintf(&s, "mov%s `s0, `d0\n", suff(exp));
            emit(state, Assm_move(s, param_reg, src));

            return temp_list_cons(
                    param_reg,
                    munch_args(state, arg_idx + 1, exp->te_list)
                    );
        } else if (exp->te_size <= 16) {
            param_reg.temp_size = word_size;
            assert(arg_idx + 1 < NELEMS(argregs));
            var param_reg2 = argregs[arg_idx + 1];
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
            assert(0 && "go away large arguments");
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

    assert(exp->te_size <= 16);
    switch (exp->te_tag) {
        case TREE_EXP_MEM:
        {
            // we can add some more special, larger ones later
            // ## loads
            var addr = exp->te_mem_addr;

            assert(exp->te_size <= 8); // expect to fail

            if (addr->te_tag == TREE_EXP_BINOP
                    && addr->te_binop == TREE_BINOP_PLUS) {
                // MEM(BINOP(+, e1, CONST))
                if (addr->te_rhs->te_tag == TREE_EXP_CONST) {
                    var r = new_temp_for_exp(state.temp_state, exp);
                    char* s = NULL;
                    Asprintf(&s, "mov%s %d(`s0), `d0\n", suff(exp),
                            addr->te_rhs->te_const);
                    var src_list = temp_list(Munch_exp(addr->te_lhs));
                    emit(state, Assm_oper(s, temp_list(r), src_list, NULL));
                    return r;
                }
                // MEM(BINOP(+, CONST, e1))
                else if (addr->te_lhs->te_tag == TREE_EXP_CONST) {
                    fprintf(stderr, "$$$ MEM(BINOP(+, CONST, e1))\n");
                }
                // MEM(BINOP(+, e1, e2))
                else {
                    fprintf(stderr, "$$$ MEM(BINOP(+, e1, e2))\n");
                }
            }

            // MEM(e1)
            var r = new_temp_for_exp(state.temp_state, exp);
            char* s = NULL;
            Asprintf(&s, "mov%s (`s0), `d0\n", suff(exp));
            var src_list = temp_list(Munch_exp(addr));
            emit(state,
                 Assm_oper(s, temp_list(r), src_list, NULL));
            return r;
        }
        case TREE_EXP_BINOP:
        {
            switch (exp->te_binop) {
                case TREE_BINOP_PLUS:
                {
                    // BINOP(+, e1, CONST)
                    if (exp->te_rhs->te_tag == TREE_EXP_CONST) {
                        temp_t r = new_temp_for_exp(state.temp_state, exp);
                        char* s = NULL;
                        Asprintf(&s, "mov%s `s0, `d0\n", suff(exp));
                        var lhs = Munch_exp(exp->te_lhs);
                        emit(state, Assm_move(s, r, lhs));

                        Asprintf(&s, "add%s $%d, `d0\n", suff(exp),
                                exp->te_rhs->te_const);
                        // r must also be in the sources as x86 reads from
                        // this register as well as writing to it
                        var src_list = temp_list(r);
                        emit(state,
                            Assm_oper(s, temp_list(r), src_list, NULL));
                        return r;
                    }
                    // BINOP(+, CONST, e1)
                    else if (exp->te_lhs->te_tag == TREE_EXP_CONST) {
                        fprintf(stderr, "$$$ BINOP(+, CONST, e1)\n");
                    }

                    // BINOP(+, e1, e2)
                    // this has to be done in two instructions due to the
                    // ciscness. but hopefully the reg-allocator can elide the
                    // first move
                    temp_t r = new_temp_for_exp(state.temp_state, exp);
                    char* s = NULL;
                    Asprintf(&s, "mov%s `s0, `d0\n", suff(exp));
                    var lhs = Munch_exp(exp->te_lhs);
                    emit(state, Assm_move(s, r, lhs));

                    Asprintf(&s, "add%s `s0, `d0\n", suff(exp));
                    // r has to be in both sources and destinations
                    var src_list =
                        temp_list_cons(Munch_exp(exp->te_rhs),
                                temp_list(r));
                    emit(state,
                         Assm_oper(s, temp_list(r), src_list, NULL));
                    return r;
                }
                case TREE_BINOP_MINUS:
                {
                    temp_t r = new_temp_for_exp(state.temp_state, exp);
                    char* s = NULL;
                    Asprintf(&s, "mov%s `s0, `d0\n", suff(exp));
                    var lhs = Munch_exp(exp->te_lhs);
                    emit(state, Assm_move(s, r, lhs));

                    Asprintf(&s, "sub%s `s0, `d0\n", suff(exp));
                    // r has to be in both sources and destinations
                    var src_list =
                        temp_list_cons(Munch_exp(exp->te_rhs),
                                temp_list(r));
                    emit(state,
                         Assm_oper(s, temp_list(r), src_list, NULL));
                    return r;
                }
                case TREE_BINOP_MUL:
                {
                    // TODO: more cases, incl w/ consts or maybe mem refs
                    temp_t r = new_temp_for_exp(state.temp_state, exp);
                    char* s = NULL;
                    Asprintf(&s, "mov%s `s0, `d0\n", suff(exp));
                    var lhs = Munch_exp(exp->te_lhs);
                    emit(state, Assm_move(s, r, lhs));

                    Asprintf(&s, "imul%s `s0, `d0\n", suff(exp));
                    // r has to be in both sources and destinations
                    var src_list =
                        temp_list_cons(Munch_exp(exp->te_rhs),
                                temp_list(r));
                    emit(state,
                         Assm_oper(s, temp_list(r), src_list, NULL));
                    return r;
                }
                case TREE_BINOP_DIV:
                {
                    // BINOP(/, e1, e2)

                    // division on x86 is a bit crazy
                    // idiv t0
                    // <=>
                    // rax <- rdx:rax / t0
                    // rdx <- rdx:rax mod t0
                    //
                    // so need to clear rdx, put rax as both src and dest
                    // and put rax, rdx and t0 in src list

                    temp_t rhs = Munch_exp(exp->te_rhs);

                    char* s = NULL;
                    Asprintf(&s, "mov%s `s0, `d0\n", suff(exp));
                    temp_t rax = special_regs[0];
                    rax.temp_size = exp->te_size;
                    var lhs = Munch_exp(exp->te_lhs);
                    emit(state, Assm_move(s, rax, lhs));

                    // There must be no munch between the following two
                    // instructions
                    temp_t rdx = special_regs[1];
                    Asprintf(&s, "xorq `s0, `d0\n");
                    emit(state,
                         Assm_oper(s, temp_list(rdx), temp_list(rdx), NULL));

                    Asprintf(&s, "idiv%s `s0\n", suff(exp));
                    // r has to be in both sources and destinations
                    var src_list =
                        temp_list_cons(rhs,
                                temp_list_cons(rax,
                                    temp_list(rdx)));
                    emit(state,
                         Assm_oper(s, temp_list(rax), src_list, NULL));

                    // Move the result out of rax again to keep it free
                    temp_t r = new_temp_for_exp(state.temp_state, exp);
                    Asprintf(&s, "mov%s `s0, `d0\n", suff(exp));
                    emit(state, Assm_move(s, r, rax));
                    return r;
                }
                case TREE_BINOP_AND:
                case TREE_BINOP_OR:
                case TREE_BINOP_XOR:
                {
                    temp_t r = new_temp_for_exp(state.temp_state, exp);
                    char* s = NULL;
                    Asprintf(&s, "mov%s `s0, `d0\n", suff(exp));
                    var lhs = Munch_exp(exp->te_lhs);
                    emit(state, Assm_move(s, r, lhs));

                    const char* instr_prefix =
                        (exp->te_binop == TREE_BINOP_AND) ? "and"
                        : (exp->te_binop == TREE_BINOP_OR) ? "or"
                        : (exp->te_binop == TREE_BINOP_XOR) ? "xor"
                        : (assert(!"broken binop case"), NULL)
                    ;
                    Asprintf(&s, "%s%s `s0, `d0\n", instr_prefix, suff(exp));
                    // r has to be in both sources and destinations
                    var src_list =
                        temp_list_cons(Munch_exp(exp->te_rhs),
                                temp_list(r));
                    emit(state,
                         Assm_oper(s, temp_list(r), src_list, NULL));
                    return r;
                }
                case TREE_BINOP_LSHIFT:
                case TREE_BINOP_RSHIFT:
                case TREE_BINOP_ARSHIFT:
                {
                    const char* instr_prefix =
                        (exp->te_binop == TREE_BINOP_LSHIFT) ? "shl"
                        : (exp->te_binop == TREE_BINOP_RSHIFT) ? "shr"
                        : (exp->te_binop == TREE_BINOP_ARSHIFT) ? "sar"
                        : (assert(!"broken binop case"), NULL)
                    ;
                    temp_t r = new_temp_for_exp(state.temp_state, exp);
                    char* s = NULL;
                    Asprintf(&s, "mov%s `s0, `d0\n", suff(exp));
                    var lhs = Munch_exp(exp->te_lhs);
                    emit(state, Assm_move(s, r, lhs));

                    // BINOP(<<, e1, CONST)
                    // BINOP(>>, e1, CONST)
                    // BINOP(>>>, e1, CONST)
                    if (exp->te_rhs->te_tag == TREE_EXP_CONST) {
                        Asprintf(&s, "%s%s $%d, `d0\n", instr_prefix, suff(exp),
                                exp->te_rhs->te_const);
                        // r has to be in both sources and destinations
                        var src_list = temp_list(r);
                        emit(state,
                            Assm_oper(s, temp_list(r), src_list, NULL));
                        return r;
                    }
                    // TODO: The rhs must be in cl (lower part of cx,ecx,rcx)

                    Asprintf(&s, "%s%s `s0, `d0\n", instr_prefix, suff(exp));
                    // r has to be in both sources and destinations
                    var src_list =
                        temp_list_cons(Munch_exp(exp->te_rhs),
                                temp_list(r));
                    emit(state,
                         Assm_oper(s, temp_list(r), src_list, NULL));
                    return r;
                }
            }
        }
        case TREE_EXP_CONST:
        {
            assert(exp->te_size <= 8);
            temp_t r = new_temp_for_exp(state.temp_state, exp);
            char* s = NULL;
            Asprintf(&s, "mov%s $%d, `d0\n", suff(exp), exp->te_const);
            emit(state, Assm_oper(s, temp_list(r), NULL, NULL));
            return r;
        }
        case TREE_EXP_TEMP:
        {
            // not sure if we have larger temps, or how we would handle them...
            assert(exp->te_size <= 8);
            return exp->te_temp;
        }
        case TREE_EXP_NAME:
        {
            temp_t r = new_temp_for_exp(state.temp_state, exp);
            char* s = NULL;
            Asprintf(&s, "leaq	%s(%%rip), `d0\n", exp->te_name);
            emit(state, Assm_oper(s, temp_list(r), NULL, NULL));
            return r;
        }
        case TREE_EXP_CALL:
        {
            assert(exp->te_size <= 8 && "TODO larger sizes");

            var func = exp->te_func;
            var args = exp->te_args;
            if (func->te_tag == TREE_EXP_NAME) {
                char* s = NULL;
                Asprintf(&s, "call %s\n", func->te_name);
                emit(state, Assm_oper(s,
                            calldefs(state.ret_arena),
                            munch_args(state, 0, args),
                            NULL));
            }
            // indirect call
            else {
                char* s = strdup_arena(state.ret_arena, "callq *`s0\n");

                emit(state, Assm_oper(s,
                            calldefs(state.ret_arena),
                            temp_list_cons(Munch_exp(func),
                                munch_args(state, 0, args)),
                            NULL));
            }
            temp_t result = state.frame->acf_target->tgt_ret0;
            result.temp_size = exp->te_size;
            assert(result.temp_size);
            return result;
        }
        case TREE_EXP_ESEQ:
        {
            assert(0 && "eseqs should no longer exist");
        }
    }
#undef temp_list
#undef temp_list_cons
#undef Munch_exp
}

// TODO: We might need to consider sign-extending and zero extending moves
// that move from smaller to larger registers

static void munch_stm(codegen_state_t state, tree_stm_t* stm)
{
#define Munch_stm(_stm) munch_stm(state, _stm)
#define Munch_exp(_exp) munch_exp(state, _exp)
#define temp_list(t) temp_list(t, state.ret_arena)
#define temp_list_cons(h, t) temp_list_cons(h, t, state.ret_arena)

    switch (stm->tst_tag) {
        case TREE_STM_SEQ:
            // Not sure we actually expect to ever see this case...
            Munch_stm(stm->tst_seq_s1);
            Munch_stm(stm->tst_seq_s2);
            break;
        case TREE_STM_MOVE:
        {
            // lucky for us the only MOVE cases produced are MOVE(MEM(..), ..)
            // and MOVE(TEMP, ..)

            // # movq
            // ## store
            // movq %rcx, (%rsi,%rax,8)
            // mov %rax, -8(%rsi)
            // mov %rax, (%rbx)
            //
            // # simple move
            // mov %rax, %rbx

            var src = stm->tst_move_exp;
            var dst = stm->tst_move_dst;
            // ## store
            if (dst->te_tag == TREE_EXP_MEM) {
                var addr = dst->te_mem_addr;
                if (addr->te_tag == TREE_EXP_BINOP) {
                    if (addr->te_binop == TREE_BINOP_PLUS) {
                        // MOVE(MEM(BINOP(+,e1,BINOP(*,e2, CONST(i)))), e3)
                        if (addr->te_rhs->te_tag == TREE_EXP_BINOP
                                && addr->te_rhs->te_binop == TREE_BINOP_MUL
                                && addr->te_rhs->te_rhs->te_tag == TREE_EXP_CONST
                                && (addr->te_rhs->te_rhs->te_const == 1
                                    || addr->te_rhs->te_rhs->te_const == 2
                                    || addr->te_rhs->te_rhs->te_const == 4
                                    || addr->te_rhs->te_rhs->te_const == 8) > 0) {
                            int scale = addr->te_rhs->te_rhs->te_const;
                            char* s = NULL;
                            Asprintf(&s, "mov%s `s2, (`s0,`s1,%d)\n",
                                    suff(src), scale);
                            var src_list =
                                temp_list_cons(Munch_exp(addr->te_lhs),
                                    temp_list_cons(Munch_exp(addr->te_rhs->te_lhs),
                                        temp_list(Munch_exp(src))));
                            emit(state,
                                 Assm_oper(
                                     s,
                                     NULL, // dst list
                                     src_list,
                                     NULL)); // jump=None
                        }
                        // MOVE(MEM(BINOP(+,e1,CONST(i))), e2)
                        else if (addr->te_rhs->te_tag == TREE_EXP_CONST) {
                            char* s = NULL;
                            Asprintf(&s, "mov%s `s1, %d(`s0)\n",
                                    suff(src), addr->te_rhs->te_const);

                            var src_list =
                                temp_list_cons(Munch_exp(addr->te_lhs),
                                        temp_list(Munch_exp(src)));
                            emit(state,
                                 Assm_oper(
                                     s,
                                     NULL, /* dst list */
                                     src_list,
                                     NULL /* jump=None */));
                        }
                        // MOVE(MEM(BINOP(+,CONST(i), e1)), e2)
                        else if (addr->te_lhs->te_tag == TREE_EXP_CONST) {
                            // basically the same as 1 above but + args reversed
                            char* s = NULL;
                            Asprintf(&s, "mov%s `s1, %d(`s0)\n", suff(src),
                                    addr->te_lhs->te_const);
                            var src_list =
                                temp_list_cons(Munch_exp(addr->te_rhs),
                                        temp_list(Munch_exp(src)));
                            emit(state,
                                 Assm_oper(s, NULL, src_list, NULL));
                        }
                        // MOVE(MEM(BINOP(+,e1,e2)),e3)
                        else {
                            char* s = NULL;
                            Asprintf(&s, "mov%s `s2, (`s0,`s1,1)\n", suff(src));
                            var src_list =
                                temp_list_cons(Munch_exp(addr->te_lhs),
                                        temp_list_cons(
                                                Munch_exp(addr->te_rhs),
                                                temp_list(Munch_exp(src))));
                            emit(state, Assm_oper(s, NULL, src_list, NULL));
                        }
                    } else {
                        // not sure we would expect a multiply or anything like
                        // that
                        assert(0 && "unexpected binop in mem");
                    }
                }
                // TODO: MOVE(MEM(CONST), e1) // not sure if we even need this
                else if (addr->te_tag == TREE_EXP_CONST) {
                    assert(0 && "todo: store to constant mem location");
                }
                // MOVE(MEM(e1), e2)
                else {
                    char* s = NULL;
                    Asprintf(&s, "mov%s `s1, (`s0)\n", suff(src));
                    var src_list =
                        temp_list_cons(Munch_exp(addr),
                                temp_list(Munch_exp(src)));
                    emit(state, Assm_oper(s, NULL, src_list, NULL));
                }
            } else if (dst->te_tag == TREE_EXP_TEMP) {
                // movq %rbx, %rax
                // movq $7, %rax
                if (src->te_tag == TREE_EXP_CONST) {
                    if (dst->te_temp.temp_size == 0) {
                        // Omit size 0 move.
                        #ifndef NDEBUG
                            tree_printf(stderr, "dropping dead code: %S\n", stm);
                        #endif
                        break;
                    }

                    char* s = NULL;
                    Asprintf(&s, "mov%s $%d, `d0\n", suff(src), src->te_const);
                    emit(state,
                         Assm_oper(s, temp_list(dst->te_temp), NULL, NULL));
                }
                // MOVE(TEMP t, TEMP t) -- this is covered by the munch
                // MOVE(TEMP t, e1)
                else {
                    temp_t src_t = Munch_exp(src);
                    assert (src_t.temp_size == dst->te_temp.temp_size);
                    if (src_t.temp_size == 0) {
                        // Omit size 0 move.
                        #ifndef NDEBUG
                            tree_printf(stderr, "dropping dead code: %S\n", stm);
                        #endif
                        break;
                    }

                    char* s = NULL;
                    Asprintf(&s, "mov%s `s0, `d0\n", suff(src));
                    emit(state,
                         Assm_move(s, dst->te_temp, src_t));
                }
            } else {
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
            // All non-function calls in statement position have no effect
            // on the world. e.g.
            // fn f() -> int { 2 - 1; 0 }
            //                 ^---^
            //                  This can be dropped
            // Maybe in the future we can emit an error
            if (stm->tst_exp->te_tag != TREE_EXP_CALL) {
                #ifndef NDEBUG
                    tree_printf(stderr, "dropping dead code: %S\n", stm);
                #endif
                break;
            }
            var func = stm->tst_exp->te_func;
            var args = stm->tst_exp->te_args;
            if (func->te_tag == TREE_EXP_NAME) {
                char* s = NULL;
                Asprintf(&s, "call %s\n", func->te_name);
                emit(state, Assm_oper(s,
                            calldefs(state.ret_arena),
                            munch_args(state, 0, args),
                            NULL));
            }
            // indirect call
            else {
                char* s = strdup_arena(state.ret_arena, "callq *`s0\n");

                emit(state, Assm_oper(s,
                            calldefs(state.ret_arena),
                            temp_list_cons(Munch_exp(func),
                                munch_args(state, 0, args)),
                            NULL));
            }
            break;
        }
        case TREE_STM_CJUMP:
        {
            // I'm not sure if we benefit much by doing a generic mem and not
            // going whole hog to try include the most generic modes
            // TODO: CJUMP(==, MEM(e1), e2, Ltrue, Lfalse) // maybe?
            // maybe better to do
            // CJUMP(==, MEM(BINOP(+, e1, CONST)), e2, ...)
            var lhs = stm->tst_cjump_lhs;
            if (lhs->te_tag == TREE_EXP_MEM
                    && lhs->te_mem_addr->te_tag == TREE_EXP_BINOP
                    && lhs->te_mem_addr->te_binop == TREE_BINOP_PLUS
                    && lhs->te_mem_addr->te_rhs->te_tag == TREE_EXP_CONST) {
                char* s = NULL;
                Asprintf(&s, "cmp%s `s1, %d(`s0)\n", suff(lhs),
                        lhs->te_mem_addr->te_rhs->te_const);
                var src_list =
                    temp_list_cons(Munch_exp(lhs->te_mem_addr->te_lhs),
                            temp_list(Munch_exp(stm->tst_cjump_rhs)));
                emit(state, Assm_oper(s, NULL, src_list, NULL));
            }
            // CJUMP(op, e1, CONST i, Ltrue, Lfalse)
            else if (stm->tst_cjump_rhs->te_tag == TREE_EXP_CONST) {
                char* s = NULL;
                Asprintf(&s, "cmp%s $%d, `s0\n", suff(stm->tst_cjump_rhs),
                        stm->tst_cjump_rhs->te_const);
                var src_list = temp_list(Munch_exp(stm->tst_cjump_lhs));
                emit(state, Assm_oper(s, NULL, src_list, NULL));
            }
            // CJUMP(==, CONST i, e1, Ltrue, Lfalse)
            else if (stm->tst_cjump_lhs->te_tag == TREE_EXP_CONST
                    && (stm->tst_cjump_op == TREE_RELOP_EQ
                        || stm->tst_cjump_op == TREE_RELOP_NE)) {
                // this is actually the harder version, since the const is on
                // the wrong side for x86
                char* s = NULL;
                Asprintf(&s, "cmp%s $%d, `s0\n", suff(stm->tst_cjump_lhs),
                        stm->tst_cjump_lhs->te_const);
                var src_list = temp_list(Munch_exp(stm->tst_cjump_rhs));
                emit(state, Assm_oper(s, NULL, src_list, NULL));
                // TODO: we could also implement comparisons by changing the
                // op...
            }
            // CJUMP(op, e1, e2, Ltrue, Lfalse)
            else {
                char* s = NULL;
                Asprintf(&s, "cmp%s `s1, `s0\n", suff(stm->tst_cjump_lhs));
                var src_list = temp_list_cons(
                        Munch_exp(stm->tst_cjump_lhs),
                        temp_list(Munch_exp(stm->tst_cjump_rhs)));
                emit(state, Assm_oper(s, NULL, src_list, NULL));
            }

            sl_sym_t* jump = ret_alloc(state, 3 * sizeof *jump);
            jump[0] = stm->tst_cjump_true;
            jump[1] = stm->tst_cjump_false;

            const char* op;
            switch (stm->tst_cjump_op) {
                case TREE_RELOP_EQ: op = "je"; break;
                case TREE_RELOP_NE: op = "jne"; break;
                case TREE_RELOP_GT: op = "jg"; break;
                case TREE_RELOP_GE: op = "jge"; break;
                case TREE_RELOP_LT: op = "jl"; break;
                case TREE_RELOP_LE: op = "jle"; break;
                case TREE_RELOP_ULT: op = "ja"; break;
                case TREE_RELOP_ULE: op = "jae"; break; // not sure if thats a real instruction
                case TREE_RELOP_UGT: op = "jb"; break;
                case TREE_RELOP_UGE: op = "jbe"; break;
            }
            char* s = NULL;
            Asprintf(&s, "%s %s\n", op, stm->tst_cjump_true);
            emit(state, Assm_oper(s, NULL, NULL, jump));
            break;
        }
        case TREE_STM_JUMP:
        {
            if (stm->tst_jump_num_labels == 1) {
                char* s = NULL;
                Asprintf(&s, "jmp %s\n", stm->tst_jump_labels[0]);
                sl_sym_t* jump = ret_alloc(state, 2 * sizeof *jump);
                jump[0] = stm->tst_jump_labels[0];
                emit(state, Assm_oper(s, NULL, NULL, jump));
            } else {
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

assm_instr_t*
x86_64_codegen(
        Arena_T instr_arena, Arena_T frag_arena, temp_state_t* temp_state,
        sl_fragment_t* fragment, tree_stm_t* stm)
{
    assm_instr_t* result = NULL;
    codegen_state_t codegen_state = {
        .ilist = &result,
        .temp_state = temp_state,
        .frame = fragment->fr_frame,
        .ret_arena = instr_arena,
        .frag_arena = frag_arena,
    };
    munch_stm(codegen_state, stm);
    return assm_list_reverse(result);
}


/*
 * proc_entry_exit_2 defines which temporaries i.e. registers are live-out
 * of the function
 */
static assm_instr_t*
x86_64_proc_entry_exit_2(ac_frame_t* frame, assm_instr_t* body, Arena_T ar)
{
    temp_list_t* src_list = NULL;
    for (int i = 0; i < NELEMS(callee_saves); i++) {
        src_list = temp_list_cons(callee_saves[i], src_list, ar);
    }
    src_list = temp_list_cons(SP, src_list, ar); // sp =rsp
    src_list = temp_list_cons(FP, src_list, ar); // fp =rbp
    temp_t ret0 = frame->acf_target->tgt_ret0;
    ret0.temp_size = word_size;
    src_list = temp_list_cons(ret0, src_list, ar); // rax
    temp_t ret1 = frame->acf_target->tgt_ret1;
    ret1.temp_size = word_size;
    src_list = temp_list_cons(ret1, src_list, ar); // rdx

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




/* An example empty linux function

	.globl	f                               # -- Begin function f
	.p2align	4, 0x90
	.type	f,@function
f:                                      # @f
.Lfunc_begin0:
	.file	1 "/Users/daniel/src/c/tmp" "xxx.c"
	.loc	1 4 0                           # xxx.c:4:0
	.cfi_startproc
# %bb.0:
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset %rbp, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register %rbp
	movl	%edi, -4(%rbp)
.Ltmp0:
	.loc	1 5 12 prologue_end             # xxx.c:5:12
	movl	-4(%rbp), %eax
	.loc	1 5 5 is_stmt 0                 # xxx.c:5:5
	popq	%rbp
	.cfi_def_cfa %rsp, 8
	retq
.Ltmp1:
.Lfunc_end0:
	.size	f, .Lfunc_end0-f
	.cfi_endproc
                                        # -- End function
*/

assm_fragment_t
x86_64_proc_entry_exit_3(ac_frame_t* frame, assm_instr_t* body, Arena_T ar)
{
    const char* fn_label = frame->acf_name;
    int frame_size = ac_frame_words(frame)*word_size;
    char* prologue = NULL;
    asprintf_arena(ar, &prologue, "\
	.globl	%s\n\
	.p2align	4, 0x90\n\
	.type	%s,@function\n\
%s:\n\
	.cfi_startproc\n\
	pushq	%%rbp\n\
	movq	%%rsp, %%rbp\n\
	subq	$%d, %%rsp\n\
",
            fn_label, fn_label, fn_label, frame_size);

    char* epilogue = NULL;
    asprintf_arena(ar, &epilogue, "\
	addq	$%d, %%rsp\n\
	popq	%%rbp\n\
	retq\n\
	.cfi_endproc\n\
",
            frame_size);
    // TODO function size ?
    return (assm_fragment_t){
        .asf_prologue = prologue,
        .asf_instrs = body,
        .asf_epilogue = epilogue,
    };
}

static void
emit_text_segment_header(FILE* out)
{
    fprintf(out, "\t.text\n");
}

void emit_str_escaped(FILE* out, const char* str);

static void
emit_data_segment(
        FILE* out, const sl_fragment_t* fragments, Table_T label_to_cs_bitmap)
{
    // For lack of better names
#define A(mnemonic, opsfmt, ...) \
    fprintf(out, "\t" mnemonic "\t" opsfmt "\n", ##__VA_ARGS__)
#define L(label) fprintf(out, "%s:\n", label)

    // No idea what this means, but it's what clang outputs
    A(".section", ".rodata.str1.1,\"aMS\",@progbits,1");

    for (var frag = fragments; frag; frag = frag->fr_list) {
        switch (frag->fr_tag) {
            case FR_CODE:
                continue;
            case FR_STRING:
            {
                A(".type", "%s,@object", frag->fr_label);
                L(frag->fr_label);

                size_t required = fmt_escaped_len(frag->fr_string);
                assert(required < 512);
                char buf[512];
                fmt_snprint_escaped(buf, 512, frag->fr_string);
                A(".asciz", "%s", buf);
                A(".size", "%s, %zu", frag->fr_label,
                        // +1 for the terminating null
                        strlen(frag->fr_string) + 1);
                break;
            }
            case FR_FRAME_MAP:
            {
                assert(!"TODO: frame map");
                break;
            }
        }
    }

    // The gas manual explains the .section syntax somewhat.
    // https://ftp.gnu.org/old-gnu/Manuals/gas-2.9.1/html_chapter/as_7.html#SEC119

    // Emit a NULL ptr until we implement the frame maps proper for x86_64.
    A(".type", "sl_rt_frame_maps,@object");
    A(".section", ".data.rel.ro,\"aw\",@progbits");
    A(".globl", "sl_rt_frame_maps");
    A(".p2align", "3, 0x0");
    L("sl_rt_frame_maps");
    A(".quad", "0");
    A(".size", "sl_rt_frame_maps, 4");

    // Silence warning about executable stack.
    A(".section", ".note.GNU-stack,\"\",@progbits");
#undef L
#undef A
}


/*
 * Here down is the implementation of the target types declared in target.h
 */

/* Used for creating the initial tempMap
 */
const temp_t x86_64_temp_map_temps[] = {
    {.temp_id = 0}, {.temp_id = 1}, {.temp_id = 2}, {.temp_id = 3},
    {.temp_id = 4}, {.temp_id = 5}, {.temp_id = 6}, {.temp_id = 7},
    {.temp_id = 8}, {.temp_id = 9}, {.temp_id = 10}, {.temp_id = 11},
    {.temp_id = 12}, {.temp_id = 13}, {.temp_id = 14}, {.temp_id = 15},
};
const char* x86_64_registers[] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};

static_assert(
        NELEMS(x86_64_temp_map_temps) == NELEMS(x86_64_registers),
        "x86_64_temp_map_temps length");

static codegen_t x86_64_codegen_module = {
    .codegen = x86_64_codegen,
    .proc_entry_exit_2 = x86_64_proc_entry_exit_2,
    .proc_entry_exit_3 = x86_64_proc_entry_exit_3,
    .load_temp = x86_64_load_temp,
    .store_temp = x86_64_store_temp,
    .emit_text_segment_header = emit_text_segment_header,
    .emit_data_segment = emit_data_segment,
};

static Table_T x86_64_temp_map();

const target_t target_x86_64 = {
    .word_size = 8,
    .stack_alignment = 16,
    .arg_registers = {
        .length = NELEMS(argregs),
        .elems = argregs,
    },
    .tgt_sp = {.temp_id = 4, .temp_size = 8}, // rsp
    .tgt_fp = {.temp_id = 5, .temp_size = 8}, // rbp
    .tgt_ret0 = {.temp_id = 0}, // rax
    .tgt_ret1 = {.temp_id = 2}, // rdx
    .callee_saves = {
        .length = NELEMS(callee_saves),
        .elems = callee_saves,
    },
    .register_names = x86_64_registers,
    .register_for_size = x86_64_register_for_size,
    .tgt_temp_map = x86_64_temp_map,
    .tgt_backend = &x86_64_codegen_module,
};
static_assert(NELEMS(callee_saves) <= TARGET_CS_COUNT_MAX, "callee save count");

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

static Table_T x86_64_temp_map()
{
    static Table_T result = NULL;
    if (result)
        return result;

    result = Table_new(0, cmptemp, hashtemp);
    for (int i = 0; i < NELEMS(x86_64_registers); i++) {
        Table_put(result,
                &(x86_64_temp_map_temps[i]),
                (void*)x86_64_registers[i]);
    }
    return result;
}
