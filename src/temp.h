#ifndef __TEMP_H__
#define __TEMP_H__

#include "symbols.h" // sl_sym_t

struct temp_state;
typedef struct temp_state temp_state_t;

typedef struct temp {
    int temp_id;
    unsigned temp_size;
} temp_t;

typedef struct temp_list {
    temp_t tmp_temp;
    struct temp_list* tmp_list;
} temp_list_t;

temp_state_t* temp_state_new();
void temp_state_free(temp_state_t** ts);

temp_t temp_newtemp(temp_state_t* ts, unsigned size);
// Not sure what this is for
// char* temp_makestring(temp_state_t* ts,  int tempval);

sl_sym_t temp_newlabel(temp_state_t* ts);
sl_sym_t temp_namedlabel(temp_state_t* ts, const char* name);

temp_list_t* temp_list(temp_t temp);
temp_list_t* temp_list_cons(temp_t hd, temp_list_t* tail);
temp_list_t* temp_list_concat(temp_list_t* lead, temp_list_t* tail);

#endif /* __TEMP_H__ */
