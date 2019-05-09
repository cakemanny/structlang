#ifndef __TREE_H__
#define __TREE_H__

#include "symbols.h" // sl_sym_t
#include "temp.h" // temp_t

struct tree_exp_t;
typedef struct tree_exp_t tree_exp_t;

struct tree_stm_t;
typedef struct tree_stm_t tree_stm_t;

enum tree_binop_t {
    TREE_BINOP_PLUS = 1,
    TREE_BINOP_MINUS,
    TREE_BINOP_MUL,
    TREE_BINOP_DIV,
    TREE_BINOP_AND,
    TREE_BINOP_OR,
    TREE_BINOP_LSHIFT,
    TREE_BINOP_RSHIFT,
    TREE_BINOP_ARSHIFT,
    TREE_BINOP_XOR,
};
typedef enum tree_binop_t tree_binop_t;

enum tree_relop_t {
    TREE_RELOP_EQ = 1,
    TREE_RELOP_NE,
    TREE_RELOP_LT,
    TREE_RELOP_GT,
    TREE_RELOP_LE,
    TREE_RELOP_GE,
    TREE_RELOP_ULT,
    TREE_RELOP_ULE,
    TREE_RELOP_UGT,
    TREE_RELOP_UGE,
};
typedef enum tree_relop_t tree_relop_t;

struct tree_exp_t {
    enum {
        TREE_EXP_CONST = 1,
        TREE_EXP_NAME,
        TREE_EXP_TEMP,
        TREE_EXP_BINOP,
        TREE_EXP_MEM,
        TREE_EXP_CALL,
        TREE_EXP_ESEQ,
    } te_tag;

    union {
        int te_const;
        sl_sym_t te_name;
        temp_t te_temp;
        struct {
            tree_binop_t te_binop;
            tree_exp_t* te_lhs;
            tree_exp_t* te_rhs;
        };
        tree_exp_t* te_mem_addr;
        struct {
            tree_exp_t* te_func;
            tree_exp_t* te_args; // list
        }; // CALL
        struct {
            tree_stm_t* te_eseq_stm;
            tree_exp_t* te_eseq_exp;
        }; // ESEQ
    };
    tree_exp_t* te_list;
};

struct tree_stm_t {
    enum {
        TREE_STM_MOVE = 1,
        TREE_STM_EXP,
        TREE_STM_JUMP,
        TREE_STM_CJUMP,
        TREE_STM_SEQ,
        TREE_STM_LABEL,
    } tst_tag;

    union {
        struct {
            tree_exp_t* tst_move_dst;
            tree_exp_t* tst_move_exp;
        }; // MOVE
        tree_exp_t* tst_exp; // EXP
        struct {
            tree_exp_t* tst_jump_dst;
            int tst_jump_num_labels;
            sl_sym_t* tst_jump_labels; // should be a list
        }; // JUMP
        struct {
            tree_relop_t tst_cjump_op;
            tree_exp_t* tst_cjump_lhs;
            tree_exp_t* tst_cjump_rhs;
            sl_sym_t tst_cjump_true;
            sl_sym_t tst_cjump_false;
        }; // CJUMP
        struct {
            tree_stm_t* tst_seq_s1;
            tree_stm_t* tst_seq_s2;
        }; // SEQ
        sl_sym_t tst_label; // LABEL
    };
};

tree_exp_t* tree_exp_const(int value);
tree_exp_t* tree_exp_name(sl_sym_t name);
tree_exp_t* tree_exp_temp(temp_t temp);
tree_exp_t* tree_exp_binop(tree_binop_t op, tree_exp_t* lhs, tree_exp_t* rhs);
tree_exp_t* tree_exp_mem(tree_exp_t* addr);
tree_exp_t* tree_exp_call(tree_exp_t* func, tree_exp_t* args);
tree_exp_t* tree_exp_eseq(tree_stm_t* s, tree_exp_t* e);

tree_stm_t* tree_stm_move(tree_exp_t* dst, tree_exp_t* e);
tree_stm_t* tree_stm_exp(tree_exp_t* e);
tree_stm_t* tree_stm_jump(tree_exp_t* dst, int num_labels, sl_sym_t* labels);
tree_stm_t* tree_stm_cjump(tree_relop_t op, tree_exp_t* lhs, tree_exp_t* rhs, sl_sym_t jtrue, sl_sym_t jfalse);
tree_stm_t* tree_stm_seq(tree_stm_t* s1, tree_stm_t* s2);
tree_stm_t* tree_stm_label(sl_sym_t label);

#endif /* __TREE_H__ */
