#ifndef __TEMP_H__
#define __TEMP_H__

#include "interfaces/arena.h"
#include "symbols.h" // sl_sym_t
#include <stdbool.h>

struct temp_state;
typedef struct temp_state temp_state_t;

typedef enum temp_ptr_disposition : unsigned char {
    TEMP_DISP_PTR = 1,
    TEMP_DISP_NOT_PTR,
    TEMP_DISP_INHERIT,
} temp_ptr_disposition_t;

typedef struct temp {
    int temp_id;
    unsigned char temp_size;
    temp_ptr_disposition_t temp_ptr_dispo;
} temp_t;

typedef struct temp_list {
    temp_t tmp_temp;
    struct temp_list* tmp_list;
} temp_list_t;

temp_state_t* temp_state_new();
void temp_state_free(temp_state_t** ts);

temp_t temp_newtemp(temp_state_t* ts, unsigned size, temp_ptr_disposition_t ptr_dispo);
// Not sure what this is for
// char* temp_makestring(temp_state_t* ts,  int tempval);
bool temp_is_machine(temp_t);

void temp_copy(temp_t* dst, const temp_t* src);

sl_sym_t temp_newlabel(temp_state_t* ts);
sl_sym_t temp_namedlabel(temp_state_t* ts, const char* name);
sl_sym_t temp_prefixedlabel(temp_state_t* ts, const char* name);

temp_list_t* temp_list(temp_t temp, Arena_T);
temp_list_t* temp_list_cons(temp_t hd, temp_list_t* tail, Arena_T);
temp_list_t* temp_list_concat(temp_list_t* lead, temp_list_t* tail, Arena_T);
void temp_list_free(temp_list_t** ptemp_list);

#endif /* __TEMP_H__ */
