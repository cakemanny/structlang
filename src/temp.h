#ifndef __TEMP_H__
#define __TEMP_H__

#include "symbols.h" // sl_sym_t

struct temp_state;
typedef struct temp_state temp_state_t;

typedef struct temp {
    int temp_id;
} temp_t;

temp_state_t* temp_state_new();
void temp_state_free(temp_state_t** ts);

temp_t temp_newtemp(temp_state_t* ts);
// Not sure what this is for
// char* temp_makestring(temp_state_t* ts,  int tempval);

sl_sym_t temp_newlabel(temp_state_t* ts);
sl_sym_t temp_namedlabel(temp_state_t* ts, const char* name);


#endif /* __TEMP_H__ */
