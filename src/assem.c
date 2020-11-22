#include "assem.h"
#include "mem.h"

#define var __auto_type

assm_instr_t* assm_oper(
        const char* assem, temp_list_t* dst, temp_list_t* src, sl_sym_t* jump)
{
    assm_instr_t* instr = xmalloc(sizeof *instr);
    instr->ai_tag = ASSM_INSTR_OPER;
    instr->ai_assem = assem;
    instr->ai_oper_dst = dst;
    instr->ai_oper_src = src;
    instr->ai_oper_jump = jump;
    return instr;
}

assm_instr_t* assm_label(const char* assem, sl_sym_t label)
{
    assm_instr_t* instr = xmalloc(sizeof *instr);
    instr->ai_tag = ASSM_INSTR_LABEL;
    instr->ai_assem = assem;
    instr->ai_label = label;
    return instr;
}

assm_instr_t* assm_move(const char* assem, temp_t dst, temp_t src)
{
    assm_instr_t* instr = xmalloc(sizeof *instr);
    instr->ai_tag = ASSM_INSTR_MOVE;
    instr->ai_assem = assem;
    instr->ai_move_dst = dst;
    instr->ai_move_src = src;
    return instr;
}

const char* assm_format(assm_instr_t* instr)
{
    // TODO: do better

    // TODO: replace `s0 `d0 with their temporaries

    return instr->ai_assem;
}

static assm_instr_t* list_rev_go(assm_instr_t* in, assm_instr_t* out) {
        if (!in)
            return out;
        var new_in = in->ai_list;
        var new_out = in;
        new_out->ai_list = out;
        return list_rev_go(new_in, new_out);
}

assm_instr_t* assm_list_reverse(assm_instr_t* instrs)
{
    return list_rev_go(instrs, NULL);
}

