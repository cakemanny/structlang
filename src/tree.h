#ifndef __TREE_H__
#define __TREE_H__

#include "interfaces/arena.h"
#include "symbols.h" // sl_sym_t
#include "temp.h" // temp_t
#include <stddef.h> // size_t

struct tree_typ_t;
typedef struct tree_typ_t tree_typ_t;

struct tree_exp_t;
typedef struct tree_exp_t tree_exp_t;

struct tree_stm_t;
typedef struct tree_stm_t tree_stm_t;

struct tree_typ_t {
    enum {
        TREE_TYPE_INT = 1,
        TREE_TYPE_BOOL,
        TREE_TYPE_VOID,
        TREE_TYPE_PTR,
        TREE_TYPE_PTR_DIFF,
        TREE_TYPE_STRUCT,
        // Should we have a string type here?
    } tt_tag;
    union {
        tree_typ_t* tt_pointee; // TREE_TYPE_PTR
        tree_typ_t* tt_fields; // TREE_TYPE_STRUCT
    };
    tree_typ_t* tt_list; /* used to link struct fields */
};

enum tree_binop_t {
    TREE_BINOP_PLUS = 1,
    TREE_BINOP_MINUS,
    TREE_BINOP_MUL,
    TREE_BINOP_DIV,
    TREE_BINOP_AND,
    TREE_BINOP_OR,
    TREE_BINOP_XOR,
    TREE_BINOP_LSHIFT,
    TREE_BINOP_RSHIFT,
    TREE_BINOP_ARSHIFT,
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
        }; // BINOP
        struct {
            tree_exp_t* te_mem_addr;
        }; // MEM
        struct {
            tree_exp_t* te_func;
            tree_exp_t* te_args; // list
            void* te_ptr_map;
        }; // CALL
        struct {
            tree_stm_t* te_eseq_stm;
            tree_exp_t* te_eseq_exp;
        }; // ESEQ
    };
    size_t te_size;
    tree_typ_t* te_type;
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
    tree_stm_t* tst_list;
};


tree_typ_t* tree_typ_int(Arena_T);
tree_typ_t* tree_typ_bool(Arena_T);
tree_typ_t* tree_typ_void(Arena_T);
tree_typ_t* tree_typ_ptr(Arena_T, tree_typ_t* pointee);
tree_typ_t* tree_typ_ptr_diff(Arena_T);
tree_typ_t* tree_typ_struct(Arena_T, tree_typ_t* fields /* list */);
tree_typ_t* tree_typ_append(tree_typ_t* hd, tree_typ_t* to_append);

/* the integer constant _value_ */
tree_exp_t* tree_exp_const(int value, size_t size, tree_typ_t* type);
/* symbolic constant _name_ corresonding to an assembly label */
tree_exp_t* tree_exp_name(Arena_T, sl_sym_t name);
/* a temp in the abstract machine, similar to a register, but infinitely many*/
tree_exp_t* tree_exp_temp(temp_t temp, size_t size, tree_typ_t* type);
/* evaluate lhs, rhs, and then apply op */
tree_exp_t* tree_exp_binop(tree_binop_t op, tree_exp_t* lhs, tree_exp_t* rhs);
/* the contents of size bytes of memory starting at address addr */
tree_exp_t* tree_exp_mem(tree_exp_t* addr, size_t size, tree_typ_t* type);
/* evaluate func, then args (left-to-right), then apply func to args */
tree_exp_t* tree_exp_call(tree_exp_t* func, tree_exp_t* args, size_t, tree_typ_t*, void*);
/* eval s for side-effects then e for a result */
tree_exp_t* tree_exp_eseq(tree_stm_t* s, tree_exp_t* e);

tree_exp_t* tree_exp_append(tree_exp_t* hd, tree_exp_t* to_append);

/* eval e and then move it into temp or mem reference */
tree_stm_t* tree_stm_move(tree_exp_t* dst, tree_exp_t* e);
/* eval e and then discard the results */
tree_stm_t* tree_stm_exp(tree_exp_t* e);
/* transfer control to address dst. common case is JUMP(NAME l, 1, [l]) */
tree_stm_t* tree_stm_jump(tree_exp_t* dst, int num_labels, sl_sym_t* labels);
/* eval lhs, then rhs, then compare with op, then jump to jtrue or jfalse */
tree_stm_t* tree_stm_cjump(tree_relop_t op, tree_exp_t* lhs, tree_exp_t* rhs, sl_sym_t jtrue, sl_sym_t jfalse);
/* s1 followed by s2 */
tree_stm_t* tree_stm_seq(tree_stm_t* s1, tree_stm_t* s2);
/* define a label such that NAME(label) may be the target of jumps */
tree_stm_t* tree_stm_label(sl_sym_t label);

tree_stm_t* tree_stm_append(tree_stm_t* hd, tree_stm_t* to_append);

void tree_typ_free(tree_typ_t** ptyp);
void tree_exp_free(tree_exp_t** pexp);
void tree_stm_free(tree_stm_t** pstm);

#include <stdio.h>
/* print tree expression */
void tree_printf(FILE* out, const char* fmt, ...);
void tree_exp_print(FILE* out, const tree_exp_t*);
void tree_stm_print(FILE* out, const tree_stm_t*);


/*
 * Returns the pointer disposition for a tree type.
 */
temp_ptr_disposition_t tree_dispo_from_type(const tree_typ_t* tree_type);


#endif /* __TREE_H__ */
