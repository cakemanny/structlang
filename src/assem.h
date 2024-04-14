#ifndef __ASSEM_H__
#define __ASSEM_H__
// vim:ft=c:

#include <stddef.h>
#include "temp.h"
#include "interfaces/arena.h"
#include "interfaces/table.h"
#include "target.h"

typedef struct assm_instr_t assm_instr_t;

struct assm_instr_t {
    enum {
        ASSM_INSTR_OPER = 1,
        ASSM_INSTR_LABEL,
        ASSM_INSTR_MOVE,
    } ai_tag;

    /* The assembly language instruction as a string */
    char* ai_assem;
    union {
        struct {
            temp_list_t* ai_oper_dst;
            temp_list_t* ai_oper_src;
            sl_sym_t* ai_oper_jump; // label list option
        }; // OPER
        sl_sym_t ai_label; // LABEL
        struct {
            temp_t ai_move_dst;
            temp_t ai_move_src;
        }; // MOVE
    };
    assm_instr_t* ai_list;
};

assm_instr_t* assm_oper(
        char* assem, temp_list_t* dst, temp_list_t* src, sl_sym_t* jump,
        Arena_T);

assm_instr_t* assm_label(char* assem, sl_sym_t label, Arena_T);

assm_instr_t* assm_move(char* assem, temp_t dst, temp_t src, Arena_T);

/*
 * The caller provides an adequately sized buffer `out` of size `out_len`
 * that the formatted instruction is written into.
 */
void assm_format(char* const out, const size_t out_len,
        const assm_instr_t* instr, Table_T allocation, const target_t* target);

assm_instr_t* assm_list_reverse(assm_instr_t*);

assm_instr_t* assm_list_chain(assm_instr_t* lead, assm_instr_t* tail);


typedef struct assm_fragment_t assm_fragment_t;

struct assm_fragment_t {
    char* asf_prologue;
    assm_instr_t* asf_instrs; /* list of instructions */
    char* asf_epilogue;
};

#endif /* __ASSEM_H__ */
