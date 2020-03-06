#include "tree.h"
#include "mem.h"
#include <assert.h>

typedef tree_exp_t* exp;
typedef tree_stm_t* stm;

static exp tree_exp_new(int tag)
{
    assert(tag > 0);
    assert(tag <= TREE_EXP_ESEQ);
    exp e = xmalloc(sizeof *e);
    e->te_tag = tag;
    return e;
}

exp tree_exp_const(int value)
{
    exp e = tree_exp_new(TREE_EXP_CONST);
    e->te_const = value;
    return e;
}

exp tree_exp_name(sl_sym_t name)
{
    exp e = tree_exp_new(TREE_EXP_NAME);
    e->te_name = name;
    return e;
}

exp tree_exp_temp(temp_t temp)
{
    exp e = tree_exp_new(TREE_EXP_TEMP);
    e->te_temp = temp;
    return e;
}

exp tree_exp_binop(tree_binop_t op, exp lhs, exp rhs)
{
    exp e = tree_exp_new(TREE_EXP_BINOP);
    e->te_binop = op;
    e->te_lhs = lhs;
    e->te_rhs = rhs;
    return e;
}

exp tree_exp_mem(exp addr, size_t size)
{
    exp e = tree_exp_new(TREE_EXP_MEM);
    e->te_mem_addr = addr;
    e->te_mem_size = size;
    return e;
}

exp tree_exp_call(exp func, exp args)
{
    exp e = tree_exp_new(TREE_EXP_CALL);
    e->te_func = func;
    e->te_args = args;
    return e;
}

exp tree_exp_eseq(stm s, exp e)
{
    exp e_ = tree_exp_new(TREE_EXP_ESEQ);
    e_->te_eseq_stm = s;
    e_->te_eseq_exp = e;
    return e_;
}

exp tree_exp_append(exp hd, exp to_append)
{
    if (!hd)
        return to_append;

    exp final_node = hd;
    while (final_node->te_list)
        final_node = final_node->te_list;
    final_node->te_list = to_append;

    return hd;
}

static stm tree_stm_new(int tag)
{
    assert(tag > 0);
    assert(tag <= TREE_STM_LABEL);
    stm s = xmalloc(sizeof *s);
    s->tst_tag = tag;
    return s;
}

stm tree_stm_move(exp dst, exp e)
{
    stm s = tree_stm_new(TREE_STM_MOVE);
    s->tst_move_dst = dst;
    s->tst_move_exp = e;
    return s;
}

stm tree_stm_exp(exp e)
{
    stm s = tree_stm_new(TREE_STM_EXP);
    s->tst_exp = e;
    return s;
}

stm tree_stm_jump(exp dst, int num_labels, sl_sym_t* labels)
{
    stm s = tree_stm_new(TREE_STM_JUMP);
    s->tst_jump_dst = dst;
    s->tst_jump_num_labels = num_labels;
    s->tst_jump_labels = labels; // should be a list
    return s;
}

stm tree_stm_cjump(tree_relop_t op, exp lhs, exp rhs, sl_sym_t jtrue, sl_sym_t jfalse)
{
    stm s = tree_stm_new(TREE_STM_CJUMP);
    s->tst_cjump_op = op;
    s->tst_cjump_lhs = lhs;
    s->tst_cjump_rhs = rhs;
    s->tst_cjump_true = jtrue;
    s->tst_cjump_false = jfalse;
    return s;
}

stm tree_stm_seq(stm s1, stm s2)
{
    stm s = tree_stm_new(TREE_STM_SEQ);
    s->tst_seq_s1 = s1;
    s->tst_seq_s2 = s2;
    return s;
}

stm tree_stm_label(sl_sym_t label)
{
    stm s = tree_stm_new(TREE_STM_LABEL);
    s->tst_label = label;
    return s;
}
