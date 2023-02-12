#include <assert.h>
#include <string.h>
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

const char* register_for_size(const char* regname, size_t size);

// returns chars written not including null term
static int format_temp(
        char* const out, const size_t size, temp_t t, Table_T allocation,
        const target_t* target)
{
    assert(size > 0);
    char* o = out;
    const char* allocated_reg = Table_get(allocation, &t);
    if (allocated_reg != NULL) {
        char regname_buf[8] = {};
        const char* sized_reg_name = target->register_for_size(allocated_reg, t.temp_size);
        strncpy(regname_buf, sized_reg_name, sizeof(regname_buf) - 1);
        int n = snprintf(o, size - (o - out), "%s", regname_buf);
        assert(n > 0); // rudimentary error check lol
        o += n;
    } else {
        int n = snprintf(o, size - (o - out), "t%d.%d", t.temp_id, t.temp_size);
        assert(n > 0); // rudimentary error check lol
        o += n;
    }
    // Check the remaining space in the buffer
    assert(size - (o - out) > 0);
    *o++ = '\0';
    return o - out - 1;
}

void assm_format(char* const out, const size_t out_len, assm_instr_t* instr,
        Table_T allocation, const target_t* target)
{
    switch (instr->ai_tag) {
        case ASSM_INSTR_OPER:
        {
            // replace `s0 `d0 with their temporaries
            // so, let's not go overboard
            temp_t src_array[32] = {};
            int i = 0;
            for (var ti = instr->ai_oper_src; ti; ti = ti->tmp_list, i++) {
                assert(i < 32);
                src_array[i] = ti->tmp_temp;
            }
            // need to consider the number of temps that function calls can
            // trash
            temp_t dst_array[32] = {};
            i = 0;
            for (var ti = instr->ai_oper_dst; ti; ti = ti->tmp_list, i++) {
                assert(i < 32);
                dst_array[i] = ti->tmp_temp;
            }

            static_assert(('s' & 0x1) != ('d' & 0x1), "neat trick eh ;)");
            temp_t* temp_arrays[2];
            temp_arrays['s' & 0x1] = src_array;
            temp_arrays['d' & 0x1] = dst_array;

            char c = 0;
            char* o = out;
            const char* in = instr->ai_assem;

            // indent a bit to start
            *o++ = '\t';

            while ((c = *in++) != 0) {
                if (c == '`') {
                    char s_or_d = *in++;
                    assert(s_or_d == 's' || s_or_d == 'd');
                    c = *in++;
                    int idx = c - '0';
                    assert(((unsigned)idx) < 8);
                    temp_t* temp_array = temp_arrays[s_or_d & 0x1];
                    temp_t t = temp_array[idx];
                    // at this point we would like to call our temp_formatting
                    // function
                    o += format_temp(o, out_len - (o - out), t, allocation, target);
                } else {
                    *o++ = c;
                }
                assert(o - out < out_len);
            }
            assert(o - out < out_len);
            *o++ = '\0';
            return;
        }
        case ASSM_INSTR_LABEL:
        {
            strncpy(out, instr->ai_assem, out_len - 1);
            out[out_len - 1] = '\0';
            return;
        }
        case ASSM_INSTR_MOVE:
        {
            // this shall have exactly 1 src and 1 dst each
            //
            // maybe we can do this one with scanf ?
            char* o = out;
            char parts[10][10] = {};
            int ret = sscanf(instr->ai_assem,
                    "%[^`]%3[`sd0-9]%[^`]%3[`sd0-9]%[^`]",
                    parts[0], parts[1], parts[2], parts[3], parts[4]);
            assert(ret == 5);

            // allow for either order
            assert(parts[1][1] == 's' || parts[1][1] == 'd');
            assert(parts[3][1] == 's' || parts[3][1] == 'd');

            *o++ = '\t'; // indent
            o += snprintf(o, out_len - (o - out), "%s", parts[0]);
            if (parts[1][1] == 's') {
                o += format_temp(o, out_len - (o - out), instr->ai_move_src, allocation, target);
            } else {
                o += format_temp(o, out_len - (o - out), instr->ai_move_dst, allocation, target);
            }
            o += snprintf(o, out_len - (o - out), "%s", parts[2]);
            if (parts[3][1] == 's') {
                o += format_temp(o, out_len - (o - out), instr->ai_move_src, allocation, target);
            } else {
                o += format_temp(o, out_len - (o - out), instr->ai_move_dst, allocation, target);
            }
            o += snprintf(o, out_len - (o - out), "%s", parts[4]);
            *o++ = '\0';
            return;
        }
    }
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

assm_instr_t* assm_list_chain(assm_instr_t* lead, assm_instr_t* tail)
{
    if (lead == NULL) {
        return tail;
    }

    var final = lead;
    while (final->ai_list)
        final = final->ai_list;
    final->ai_list = tail;
    return lead;
}
