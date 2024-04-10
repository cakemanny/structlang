#include "tree.h"
#include <assert.h>
#include <stdio.h> // fprintf, ...
#include <stdarg.h> // va_start, va_arg, va_end

#define var __auto_type
#define Alloc(arena, size) Arena_alloc(arena, size, __FILE__, __LINE__)

typedef tree_exp_t* exp;
typedef tree_stm_t* stm;


static tree_typ_t* tree_typ_new(int tag, Arena_T a)
{
    assert(tag > 0);
    assert(tag <= TREE_TYPE_STRUCT);
    tree_typ_t* t = Alloc(a, sizeof *t);
    t->tt_tag = tag;
    return t;
}

/*
static tree_typ_t base_types[] = {
    { .tt_tag = TREE_TYPE_INT },
    { .tt_tag = TREE_TYPE_BOOL },
    { .tt_tag = TREE_TYPE_VOID },
    { .tt_tag = TREE_TYPE_PTR_DIFF },
};
*/

tree_typ_t* tree_typ_int(Arena_T a)
{
    return tree_typ_new(TREE_TYPE_INT, a);
}

tree_typ_t* tree_typ_bool(Arena_T a)
{
    return tree_typ_new(TREE_TYPE_BOOL, a);
}

tree_typ_t* tree_typ_void(Arena_T a)
{
    return tree_typ_new(TREE_TYPE_VOID, a);
}

tree_typ_t* tree_typ_ptr(tree_typ_t* pointee, Arena_T a)
{
    var t = tree_typ_new(TREE_TYPE_PTR, a);
    t->tt_pointee = pointee;
    return t;
}

tree_typ_t* tree_typ_ptr_diff(Arena_T a)
{
    return tree_typ_new(TREE_TYPE_PTR_DIFF, a);
}

tree_typ_t* tree_typ_struct(tree_typ_t* fields, Arena_T a)
{
    var t = tree_typ_new(TREE_TYPE_STRUCT, a);
    t->tt_fields = fields;
    return t;
}

tree_typ_t* tree_typ_append(tree_typ_t* hd, tree_typ_t* to_append)
{
    tree_typ_t** p = &hd;
    while (*p)
        p = &(*p)->tt_list;
    *p = to_append;
    return hd;
}


static exp tree_exp_new(int tag, Arena_T a)
{
    assert(tag > 0);
    assert(tag <= TREE_EXP_ESEQ);
    exp e = Alloc(a, sizeof *e);
    e->te_tag = tag;
    return e;
}

exp tree_exp_const(int value, size_t size, tree_typ_t* type, Arena_T a)
{
    exp e = tree_exp_new(TREE_EXP_CONST, a);
    e->te_const = value;
    e->te_size = size;
    e->te_type = type;
    return e;
}

exp tree_exp_name(sl_sym_t name, Arena_T a)
{
    exp e = tree_exp_new(TREE_EXP_NAME, a);
    e->te_name = name;
    // FIXME: e->te_size = ??? word size?

    // could be a string, or maybe some code :shrug:
    // but it's not too important
    e->te_type = tree_typ_ptr(tree_typ_void(a), a);
    return e;
}

exp tree_exp_temp(temp_t temp, size_t size, tree_typ_t* type, Arena_T a)
{
    exp e = tree_exp_new(TREE_EXP_TEMP, a);
    e->te_temp = temp;
    assert(size == temp.temp_size);
    e->te_size = size;
    e->te_type = type;
    return e;
}

exp tree_exp_binop(tree_binop_t op, exp lhs, exp rhs, Arena_T a)
{
    exp e = tree_exp_new(TREE_EXP_BINOP, a);
    e->te_binop = op;
    e->te_lhs = lhs;
    e->te_rhs = rhs;
    // can left and right have different sizes?
    e->te_size = lhs->te_size;
    assert(lhs->te_size == rhs->te_size); // let's see

    // hopefully we can say the same thing about types being the same as
    // that of the operands
    e->te_type = lhs->te_type;
    return e;
}

exp tree_exp_mem(exp addr, size_t size, tree_typ_t* type, Arena_T a)
{
    exp e = tree_exp_new(TREE_EXP_MEM, a);
    e->te_mem_addr = addr;
    e->te_size = size;
    e->te_type = type;
    return e;
}

exp tree_exp_call(exp func, exp args, size_t size, tree_typ_t* type,
        void* ptr_map, Arena_T a)
{
    assert(size);
    exp e = tree_exp_new(TREE_EXP_CALL, a);
    e->te_func = func;
    e->te_args = args;
    e->te_size = size;
    e->te_type = type;
    e->te_ptr_map = ptr_map;
    return e;
}

exp tree_exp_eseq(stm s, exp e, Arena_T a)
{
    exp e_ = tree_exp_new(TREE_EXP_ESEQ, a);
    e_->te_eseq_stm = s;
    e_->te_eseq_exp = e;
    e_->te_size = e->te_size;
    e_->te_type = e->te_type;
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

static stm tree_stm_new(int tag, Arena_T a)
{
    assert(tag > 0);
    assert(tag <= TREE_STM_LABEL);
    stm s = Alloc(a, sizeof *s);
    s->tst_tag = tag;
    return s;
}

stm tree_stm_move(exp dst, exp e, Arena_T a)
{
    stm s = tree_stm_new(TREE_STM_MOVE, a);
    s->tst_move_dst = dst;
    s->tst_move_exp = e;
    return s;
}

stm tree_stm_exp(exp e, Arena_T a)
{
    stm s = tree_stm_new(TREE_STM_EXP, a);
    s->tst_exp = e;
    return s;
}

stm tree_stm_jump(exp dst, int num_labels, sl_sym_t* labels, Arena_T a)
{
    stm s = tree_stm_new(TREE_STM_JUMP, a);
    s->tst_jump_dst = dst;
    s->tst_jump_num_labels = num_labels;
    s->tst_jump_labels = labels; // should be a list
    return s;
}

stm tree_stm_cjump(tree_relop_t op, exp lhs, exp rhs, sl_sym_t jtrue, sl_sym_t jfalse, Arena_T a)
{
    stm s = tree_stm_new(TREE_STM_CJUMP, a);
    s->tst_cjump_op = op;
    s->tst_cjump_lhs = lhs;
    s->tst_cjump_rhs = rhs;
    s->tst_cjump_true = jtrue;
    s->tst_cjump_false = jfalse;
    return s;
}

stm tree_stm_seq(stm s1, stm s2, Arena_T a)
{
    stm s = tree_stm_new(TREE_STM_SEQ, a);
    assert(s1);
    // TODO: why do we not assert s2 ?
    s->tst_seq_s1 = s1;
    s->tst_seq_s2 = s2;
    return s;
}

stm tree_stm_label(sl_sym_t label, Arena_T a)
{
    stm s = tree_stm_new(TREE_STM_LABEL, a);
    s->tst_label = label;
    return s;
}

stm tree_stm_append(stm hd, stm to_append)
{
    stm* p = &hd;
    while (*p)
        p = &(*p)->tst_list;
    *p = to_append;
    return hd;
}


void tree_exp_print(FILE* out, const tree_exp_t* e)
{
    if (!e) {
        fprintf(out, "NULL");
        return;
    }
    switch (e->te_tag) {
        case TREE_EXP_CONST:
            fprintf(out, "CONST(%d, %lu)", e->te_const, e->te_size);
            return;
        case TREE_EXP_NAME:
            fprintf(out, "NAME(%s, %lu)", e->te_name, e->te_size);
            return;
        case TREE_EXP_TEMP:
            fprintf(out, "TEMP(%d, %lu)", e->te_temp.temp_id, e->te_size);
            return;
        case TREE_EXP_BINOP:
            tree_printf(
                    out, "BINOP(%B, %E, %E, %d)", e->te_binop,
                    e->te_lhs, e->te_rhs, e->te_size);
            return;
        case TREE_EXP_MEM:
            tree_printf(out, "MEM(%E, %d)", e->te_mem_addr, e->te_size);
            return;
        case TREE_EXP_CALL:
            tree_printf(out, "CALL(%E, [", e->te_func);
            for (var arg = e->te_args; arg; arg = arg->te_list) {
                if (arg != e->te_args) {fprintf(out, ", ");}
                tree_exp_print(out, arg);
            }
            fprintf(out, "], %lu)", e->te_size);
            return;
        case TREE_EXP_ESEQ:
            tree_printf(out, "ESEQ(%S, %E)", e->te_eseq_stm, e->te_eseq_exp);
            return;
    }
}

void tree_stm_print(FILE* out, const tree_stm_t* s)
{
    if (!s) {
        fprintf(out, "NULL");
        return;
    }
    switch (s->tst_tag) {
        case TREE_STM_MOVE:
            tree_printf(out, "MOVE(%E, %E)", s->tst_move_dst, s->tst_move_exp);
            return;
        case TREE_STM_EXP:
            tree_printf(out, "EXP(%E)", s->tst_exp);
            return;
        case TREE_STM_JUMP:
            tree_printf(out, "JUMP(%E", s->tst_jump_dst);
            for (int i = 0; i < s->tst_jump_num_labels; i++) {
                fprintf(out, ", %s", s->tst_jump_labels[i]);
            }
            fprintf(out, ")");
            return;
        case TREE_STM_CJUMP:
            tree_printf(
                    out, "CJUMP(%R, %E, %E, %s, %s)",
                    s->tst_cjump_op, s->tst_cjump_lhs, s->tst_cjump_rhs,
                    s->tst_cjump_true, s->tst_cjump_false);
            return;
        case TREE_STM_SEQ:
            tree_printf(out, "SEQ(%S, %S)", s->tst_seq_s1, s->tst_seq_s2);
            return;
        case TREE_STM_LABEL:
            fprintf(out, "LABEL(%s)", s->tst_label);
            return;
    }
}

void tree_printf(FILE* out, const char* fmt, ...)
{
    const char *cp = fmt;
    char c;

    va_list valist;
    va_start(valist, fmt);

    flockfile(out);
    while ((c = *cp++)) {
        if (c == '%') {
            c = *cp++;
            switch (c) {
                case 'c':
                {
                    int cc = va_arg(valist, int);
                    putc_unlocked(cc, out);
                    break;
                }
                case 'd':
                {
                    /* 43 is maximum possible length of 128-bit integer
                     * string representation */
                    char str[44] = {};
                    char* s = str + sizeof str - 1;
                    int n = va_arg(valist, int);
                    int m = (n < 0) ? -n : n;
                    do {
                        *--s = '0' + m%10;
                        m /= 10;
                    } while (m); // use do while to get printing for 0
                    if (n < 0) {
                        *--s = '-';
                    }
                    do {
                        putc_unlocked(*s++, out);
                    } while (*s);
                    break;
                }
                case 's':
                {
                    const char* cs = va_arg(valist, const char*);
                    while (*cs) { putc_unlocked(*cs++, out); }
                    break;
                }
                case 'S':
                {
                    funlockfile(out); // not sure if re-entrant
                    tree_stm_print(out, va_arg(valist, tree_stm_t*));
                    flockfile(out); // not sure if re-entrant
                    break;
                }
                case 'E':
                {
                    funlockfile(out); // not sure if re-entrant
                    tree_exp_print(out, va_arg(valist, tree_exp_t*));
                    flockfile(out); // not sure if re-entrant
                    break;
                }
                case 'B':
                {
                    /* binary/unary operation */
                    tree_binop_t cc = va_arg(valist, tree_binop_t);
                    char* op_str = NULL;
                    switch (cc) {
                        case TREE_BINOP_PLUS: op_str = "+"; break;
                        case TREE_BINOP_MINUS: op_str = "-"; break;
                        case TREE_BINOP_MUL: op_str = "*"; break;
                        case TREE_BINOP_DIV: op_str = "/"; break;
                        case TREE_BINOP_AND: op_str = "&"; break;
                        case TREE_BINOP_OR: op_str = "|"; break;
                        case TREE_BINOP_XOR: op_str = "^"; break;
                        case TREE_BINOP_LSHIFT: op_str = "<<"; break;
                        case TREE_BINOP_RSHIFT: op_str = ">>"; break;
                        case TREE_BINOP_ARSHIFT: op_str = ">>>"; break;
                    }
                    assert(op_str && "binop case missing");
                    const char* cs = op_str;
                    while (*cs) { putc_unlocked(*cs++, out); }
                    break;
                }
                case 'R':
                {
                    /* binary/unary operation */
                    tree_relop_t cc = va_arg(valist, tree_relop_t);
                    char* op_str = NULL;
                    switch (cc) {
                        case TREE_RELOP_EQ: op_str = "=="; break;
                        case TREE_RELOP_NE: op_str = "!="; break;
                        case TREE_RELOP_LT: op_str = "<"; break;
                        case TREE_RELOP_GT: op_str = ">"; break;
                        case TREE_RELOP_LE: op_str = "<="; break;
                        case TREE_RELOP_GE: op_str = ">="; break;
                        case TREE_RELOP_ULT: op_str = "u<"; break;
                        case TREE_RELOP_ULE: op_str = "u<="; break;
                        case TREE_RELOP_UGT: op_str = "u>"; break;
                        case TREE_RELOP_UGE: op_str = "u>="; break;
                    }
                    assert(op_str && "relop case missing");
                    const char* cs = op_str;
                    while (*cs) { putc_unlocked(*cs++, out); }
                    break;
                }
                case '%':
                    putc_unlocked(c, out);
                    break;
                case '\0': --cp; break;
                default: assert(0 && "need to add another fmt specifier");
            }
        } else {
            putc_unlocked(c, out);
        }
    }
    funlockfile(out);

    va_end(valist);
}


temp_ptr_disposition_t tree_dispo_from_type(const tree_typ_t* tree_type) {
    switch (tree_type->tt_tag) {
        case TREE_TYPE_INT:
        case TREE_TYPE_BOOL:
        case TREE_TYPE_VOID:
            return TEMP_DISP_NOT_PTR;
        case TREE_TYPE_PTR:
            return TEMP_DISP_PTR;
        case TREE_TYPE_PTR_DIFF:
            return TEMP_DISP_NOT_PTR;
        case TREE_TYPE_STRUCT:
            return TEMP_DISP_NOT_PTR;
    }
}
