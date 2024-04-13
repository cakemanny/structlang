#define _GNU_SOURCE // linux: ask for asprintf in stdio.h
#include "temp.h"
#include <stdio.h> // snprintf
#include <assert.h>
#include <stdlib.h>

#define var __auto_type
#define Alloc(arena, size) Arena_alloc(arena, size, __FILE__, __LINE__)

static const int start_temp = 100;

struct temp_state {
    int next_temp;
    int next_label;
};

temp_state_t* temp_state_new(Arena_T a)
{
    temp_state_t* ts = Alloc(a, sizeof *ts);
    ts->next_temp = start_temp;
    ts->next_label = 0;
    return ts;
}

temp_t temp_newtemp(temp_state_t* ts, unsigned size, temp_ptr_disposition_t ptr_dispo)
{
    assert(size <= 8); // register width on a 64-bit architecture
                       // forgetting vector and floating point registers
    assert(ptr_dispo != 0);

    temp_t result = {
        .temp_id = ts->next_temp++,
        .temp_size = size,
        .temp_ptr_dispo = ptr_dispo,
    };
    return result;
}

bool temp_is_machine(temp_t t)
{
    return t.temp_id < start_temp;
}

void temp_copy(temp_t* dst, const temp_t* src)
{
    dst->temp_id = src->temp_id;
    dst->temp_size = src->temp_size;
    dst->temp_ptr_dispo = src->temp_ptr_dispo;
}


sl_sym_t temp_newlabel(temp_state_t* ts)
{
    int label_id = ts->next_label++;
    char str[45];
    snprintf(str, sizeof str, "L%d", label_id); // snprintf null terminates str
    return symbol(str); // symbol memcpys str onto the heap
}

sl_sym_t temp_namedlabel(temp_state_t* ts, const char* name)
{
    return symbol(name);
}

sl_sym_t temp_prefixedlabel(temp_state_t* ts, const char* prefix)
{
    int label_id = ts->next_label++;
    char* str = NULL;
    asprintf(&str, "L%s%d", prefix, label_id);
    sl_sym_t result = symbol(str);
    free(str);
    return result;
}

temp_list_t* temp_list(temp_t temp, Arena_T ar)
{
    return temp_list_cons(temp, NULL, ar);
}

temp_list_t* temp_list_cons(temp_t hd, temp_list_t* tail, Arena_T arena)
{
    assert(hd.temp_size != 0);
    temp_list_t* list = Alloc(arena, sizeof *list);
    list->tmp_temp = hd;
    list->tmp_list = tail;
    return list;
}

/*
 * This looks like it leaks the cells of the lead list
 */
temp_list_t*
temp_list_concat(temp_list_t* lead, temp_list_t* tail, Arena_T ar)
{
    if (lead == NULL) {
        return tail;
    }
    return temp_list_cons(
            lead->tmp_temp,
            temp_list_concat(lead->tmp_list, tail, ar), ar);
}

void temp_list_free(temp_list_t** ptemp_list)
{
    while (*ptemp_list) {
        var to_free = *ptemp_list;
        *ptemp_list = (*ptemp_list)->tmp_list;
        free(to_free);
    }
}
