#include "temp.h"
#include "mem.h"
#include <stdio.h> // snprintf
#include <assert.h>

struct temp_state {
    int next_temp;
    int next_label;
};

temp_state_t* temp_state_new()
{
    temp_state_t* ts = xmalloc(sizeof *ts);
    ts->next_temp = 100;
    ts->next_label = 0;
    return ts;
}

void temp_state_free(temp_state_t** ts)
{
    free(*ts);
    *ts = NULL;
}

temp_t temp_newtemp(temp_state_t* ts, unsigned size)
{
    temp_t result = { .temp_id = ts->next_temp++, .temp_size = size };
    return result;
}

sl_sym_t temp_newlabel(temp_state_t* temp)
{
    int label_id = temp->next_label++;
    char str[45];
    snprintf(str, sizeof str, "L%d", label_id); // snprintf null terminates str
    return symbol(str); // symbol memcpys str onto the heap
}

sl_sym_t temp_namedlabel(temp_state_t* temp, const char* name)
{
    return symbol(name);
}

temp_list_t* temp_list(temp_t temp)
{
    return temp_list_cons(temp, NULL);
}

temp_list_t* temp_list_cons(temp_t hd, temp_list_t* tail)
{
    assert(hd.temp_size != 0);
    temp_list_t* list = xmalloc(sizeof *list);
    list->tmp_temp = hd;
    list->tmp_list = tail;
    return list;
}

temp_list_t* temp_list_concat(temp_list_t* lead, temp_list_t* tail)
{
    if (lead == NULL) {
        return tail;
    }
    return temp_list_cons(
            lead->tmp_temp,
            temp_list_concat(lead->tmp_list, tail));
}
