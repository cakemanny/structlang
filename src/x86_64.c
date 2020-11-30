#include "x86_64.h"
#include "mem.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h> // strdup

#define var __auto_type

#define NELEMS(A) ((sizeof A) / sizeof A[0])

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

#define Asprintf(ret, format, ...) do { \
    if (unlikely(asprintf(ret, format, ##__VA_ARGS__) < 0)) { \
        perror("out of memory"); \
        abort(); \
    } \
} while (0)

typedef struct codegen_t {
    assm_instr_t** ilist;
    temp_state_t* temp_state;
    ac_frame_t* frame;
} codegen_t;

// pre-declarations
static temp_t munch_exp(codegen_t state, tree_exp_t* exp);


temp_t special_regs[] = {
    {.temp_id = 0}, // rax  return value pt1
    {.temp_id = 2}, // rdx  return value pt2
    {.temp_id = 4}, // rsp  stack pointer
    {.temp_id = 5}, // rbp  frame pointer
};

// is this what he meant by outgoing arguments?
// not quite the same as activation.c
temp_t argregs[] = {
    {.temp_id = 7}, // rdi
    {.temp_id = 6}, // rsi
    {.temp_id = 2}, // rdx
    {.temp_id = 1}, // rcx
    {.temp_id = 8}, // r8
    {.temp_id = 9}, // r9
};

// registers that won't be trashed by a called function
temp_t callee_saves[] = {
    {.temp_id = 3}, // rbx
    // {.temp_id = 5}, // rbp  -- commented since it's in special regs
    {.temp_id = 12}, // r12
    {.temp_id = 13}, // r13
    {.temp_id = 14}, // r14
    {.temp_id = 15}, // r15
};

// things we trash
temp_t caller_saves[] = {
    {.temp_id = 10}, // r10  could be used for static chain pointer - if wanted
    {.temp_id = 11}, // r11
};

static temp_list_t* calldefs = NULL;

static void init_calldefs()
{
    temp_list_t* c = NULL;

    for (int i = 0; i < NELEMS(caller_saves); i++) {
        c = temp_list_cons(caller_saves[i], c);
    }

    // the return value registers
    c = temp_list_cons(special_regs[0], c); // rax
    // -- the next line is commented because we included it already in args
    //c = temp_list_cons(special_regs[1], c); // rdx

    // should all the argument registers be included also?
    for (int i = 0; i < NELEMS(argregs); i++) {
        c = temp_list_cons(argregs[i], c);
    }

    // we should include the return address register here
    // which one is that ??
    // I think it's just stored on the stack as part of the call
    calldefs = c;
}

static char* strdupchk(const char* s1)
{
    char* s = strdup(s1);
    if (!s) {
        perror("out of memory");
        abort();
    }
    return s;
}

static void emit(codegen_t state, assm_instr_t* new_instr)
{
    new_instr->ai_list = *state.ilist;
    *state.ilist = new_instr;
}

/**
 * return the at&t syntax instruction suffix that needs to be used for
 * the expression
 */
static const char* suff(tree_exp_t* exp)
{
    switch (exp->te_size)
    {
        case 8: return "q";
        case 4: return "l";
        case 2: return "w";
        case 1: return "b";
    }
    fprintf(stderr, "invalid size %lu\n", exp->te_size);
    assert(0 && "invalid size");
}

static temp_list_t* munch_args(codegen_t state, int arg_idx, tree_exp_t* exp)
{
    if (exp == NULL) {
        // no more remaining arguments, return empty list
        return NULL;
    }

    assert(arg_idx < NELEMS(argregs));
    var param_reg = argregs[arg_idx];

    if (exp->te_size <= 8) {
        var src = munch_exp(state, exp);
        char* s = NULL;
        Asprintf(&s, "mov%s `s0, `d0\n", suff(exp));
        emit(state, assm_move(s, param_reg, src));

        return temp_list_cons(
                param_reg,
                munch_args(state, arg_idx + 1, exp->te_list)
                );
    } else if (exp->te_size <= 16) {
        assert(arg_idx + 1 < NELEMS(argregs));
        var param_reg2 = argregs[arg_idx + 1];

        munch_exp(state, exp);
        // TODO: change munch to return a list of temps

        return temp_list_cons(
                param_reg,
                temp_list_cons(
                    param_reg2,
                    munch_args(state, arg_idx + 2, exp->te_list)));
    } else {
        assert(0 && "go away large arguments");
    }
}

static temp_t munch_exp(codegen_t state, tree_exp_t* exp)
{
#define Munch_exp(_exp) munch_exp(state, _exp)
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
                    var r = temp_newtemp(state.temp_state);
                    char* s = NULL;
                    Asprintf(&s, "mov%s %d(`s0), `d0\n", suff(exp),
                            addr->te_rhs->te_const);
                    var src_list = temp_list(Munch_exp(addr->te_lhs));
                    emit(state, assm_oper(s, temp_list(r), src_list, NULL));
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
            var r = temp_newtemp(state.temp_state);
            char* s = NULL;
            Asprintf(&s, "mov%s (`s0), `d0\n", suff(exp));
            var src_list = temp_list(Munch_exp(addr));
            emit(state,
                 assm_oper(s, temp_list(r), src_list, NULL));
            return r;
        }
        case TREE_EXP_BINOP:
        {
            switch (exp->te_binop) {
                case TREE_BINOP_PLUS:
                {
                    // BINOP(+, e1, CONST)
                    if (exp->te_rhs->te_tag == TREE_EXP_CONST) {
                        temp_t r = temp_newtemp(state.temp_state);
                        char* s = NULL;
                        Asprintf(&s, "mov%s `s0, `d0\n", suff(exp));
                        var lhs = Munch_exp(exp->te_lhs);
                        emit(state, assm_move(s, r, lhs));

                        Asprintf(&s, "add%s $%d, `d0\n", suff(exp),
                                exp->te_rhs->te_const);
                        // r must also be in the sources as x86 reads from
                        // this register as well as writing to it
                        var src_list = temp_list(r);
                        emit(state,
                            assm_oper(s, temp_list(r), src_list, NULL));
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
                    temp_t r = temp_newtemp(state.temp_state);
                    char* s = NULL;
                    Asprintf(&s, "mov%s `s0, `d0\n", suff(exp));
                    var lhs = Munch_exp(exp->te_lhs);
                    emit(state, assm_move(s, r, lhs));

                    Asprintf(&s, "add%s `s0, `d0\n", suff(exp));
                    // r has to be in both sources and destinations
                    var src_list =
                        temp_list_cons(Munch_exp(exp->te_rhs),
                                temp_list(r));
                    emit(state,
                         assm_oper(s, temp_list(r), src_list, NULL));
                    return r;
                }
                case TREE_BINOP_MUL:
                {
                    // TODO: more cases, incl w/ consts or maybe mem refs
                    temp_t r = temp_newtemp(state.temp_state);
                    char* s = NULL;
                    Asprintf(&s, "mov%s `s0, `d0\n", suff(exp));
                    var lhs = Munch_exp(exp->te_lhs);
                    emit(state, assm_move(s, r, lhs));

                    Asprintf(&s, "imul%s `s0, `d0\n", suff(exp));
                    // r has to be in both sources and destinations
                    var src_list =
                        temp_list_cons(Munch_exp(exp->te_rhs),
                                temp_list(r));
                    emit(state,
                         assm_oper(s, temp_list(r), src_list, NULL));
                    return r;
                }
                default:
                    assert(0 && "unhandled binary operator");
            }
        }
        case TREE_EXP_CONST:
        {
            assert(exp->te_size <= 8);
            temp_t r = temp_newtemp(state.temp_state);
            char* s = NULL;
            Asprintf(&s, "mov%s $%d, `d0\n", suff(exp), exp->te_const);
            emit(state, assm_oper(s, temp_list(r), NULL, NULL));
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
            // not sure we should be emitting these at this time?
            // but if we were we could do
            //
            // leaq func_name(%rip), `d0
            assert(0 && "unexpected name expression");
        }
        case TREE_EXP_CALL:
        {
            assert(exp->te_size <= 8 && "TODO larger sizes");

            var func = exp->te_func;
            var args = exp->te_args;
            if (func->te_tag == TREE_EXP_NAME) {
                char* s = NULL;
                Asprintf(&s, "call %s\n", func->te_name);
                assert(calldefs);
                emit(state, assm_oper(
                            s, calldefs, munch_args(state, 0, args), NULL));
            }
            // indirect call
            else {
                char* s = strdupchk("callq *`s0\n");

                assert(calldefs);
                emit(state, assm_oper(s,
                            calldefs,
                            temp_list_cons(Munch_exp(func),
                                munch_args(state, 0, args)),
                            NULL));
            }
            return state.frame->acf_regs->acr_ret0;
        }
        case TREE_EXP_ESEQ:
        {
            assert(0 && "eseqs should no longer exist");
        }
    }
#undef Munch_exp
}

static void munch_stm(codegen_t state, tree_stm_t* stm)
{
#define Munch_stm(_stm) munch_stm(state, _stm)
#define Munch_exp(_exp) munch_exp(state, _exp)

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
                                 assm_oper(
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
                                 assm_oper(
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
                                 assm_oper(s, NULL, src_list, NULL));
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
                            emit(state, assm_oper(s, NULL, src_list, NULL));
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
                    emit(state, assm_oper(s, NULL, src_list, NULL));
                }
            } else if (dst->te_tag == TREE_EXP_TEMP) {
                // movq %rbx, %rax
                // movq $7, %rax
                if (src->te_tag == TREE_EXP_CONST) {
                    char* s = NULL;
                    Asprintf(&s, "mov%s $%d, `d0\n", suff(src), src->te_const);
                    emit(state,
                         assm_oper(s, temp_list(dst->te_temp), NULL, NULL));
                }
                // MOVE(TEMP t, TEMP t) -- this is covered by the munch
                // MOVE(TEMP t, e1)
                else {
                    char* s = NULL;
                    Asprintf(&s, "mov%s `s0, `d0\n", suff(src));
                    emit(state,
                         assm_oper(s,
                                   temp_list(dst->te_temp),
                                   temp_list(Munch_exp(src)),
                                   NULL));
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
            emit(state, assm_label(s, stm->tst_label));
            break;
        }
        case TREE_STM_EXP:
        {
            // Can this only be a function call?
            assert(stm->tst_exp->te_tag == TREE_EXP_CALL);
            var func = stm->tst_exp->te_func;
            var args = stm->tst_exp->te_args;
            if (func->te_tag == TREE_EXP_NAME) {
                char* s = NULL;
                Asprintf(&s, "call %s\n", func->te_name);
                assert(calldefs);
                emit(state, assm_oper(
                            s, calldefs, munch_args(state, 0, args), NULL));
            }
            // indirect call
            else {
                char* s = strdupchk("callq *`s0\n");

                assert(calldefs);
                emit(state, assm_oper(s,
                            calldefs,
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
                emit(state, assm_oper(s, NULL, src_list, NULL));
            }
            // CJUMP(==, e1, CONST i, Ltrue, Lfalse)
            else if (stm->tst_cjump_rhs->te_tag == TREE_EXP_CONST) {
                fprintf(stderr, "$$$ CJUMP(==, e1, CONST i, Ltrue, Lfalse)\n");
                assert(0 && "TODO");
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
                emit(state, assm_oper(s, NULL, src_list, NULL));
            }
            // CJUMP(==, e1, e2, Ltrue, Lfalse)
            else {
                char* s = NULL;
                Asprintf(&s, "cmp%s `s1, `s0\n", suff(stm->tst_cjump_lhs));
                var src_list = temp_list_cons(
                        Munch_exp(stm->tst_cjump_lhs),
                        temp_list(Munch_exp(stm->tst_cjump_rhs)));
                emit(state, assm_oper(s, NULL, src_list, NULL));
            }

            sl_sym_t* jump = xmalloc(3 * sizeof *jump);
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
            emit(state, assm_oper(s, NULL, NULL, jump));
            break;
        }
        case TREE_STM_JUMP:
        {
            if (stm->tst_jump_num_labels == 1) {
                char* s = NULL;
                Asprintf(&s, "jmp %s\n", stm->tst_jump_labels[0]);
                sl_sym_t* jump = xmalloc(2 * sizeof *jump);
                jump[0] = stm->tst_jump_labels[0];
                emit(state, assm_oper(s, NULL, NULL, jump));
            } else {
                assert(0 && "TODO: switch");
            }
            break;
        }
    }
#undef Munch_exp
#undef Munch_stm
}

assm_instr_t* x86_64_codegen(
        temp_state_t* temp_state, ac_frame_t* frame, tree_stm_t* stm)
{
    if (!calldefs) {
        init_calldefs();
    }
    assm_instr_t* result = NULL;
    codegen_t codegen_state = {
        .ilist = &result,
        .temp_state = temp_state,
        .frame = frame,
    };
    munch_stm(codegen_state, stm);
    return assm_list_reverse(result);
}

assm_instr_t* proc_entry_exit_2(ac_frame_t* frame, assm_instr_t* body)
{
    temp_list_t* src_list = NULL;
    for (int i = 0; i < NELEMS(callee_saves); i++) {
        src_list = temp_list_cons(callee_saves[i], src_list);
    }
    src_list = temp_list_cons(special_regs[2], src_list);
    src_list = temp_list_cons(special_regs[3], src_list);

    sl_sym_t* empty_jump_list = xmalloc(sizeof *empty_jump_list);

    var sink_instr = assm_oper(
            strdupchk(""),
            NULL,
            src_list,
            empty_jump_list);

    // append sink instruction to body
    var b = body;
    while (b->ai_list) {
        b = b->ai_list;
    }
    b->ai_list = sink_instr;

    return body;
}
