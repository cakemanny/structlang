#ifndef __REG_ALLOC_H__
#define __REG_ALLOC_H__
// vim:ft=c:

#include "assem.h" // assm_instr_t
#include "activation.h" // ac_frame_t
#include "interfaces/table.h"
#include "liveness.h"

/* type allocation = Table[temp_t, Frame.register] */

typedef struct ra_reg_alloc_t {
} ra_reg_alloc_t;

/**
 * val alloc : Assem.instr list * Frame.frame -> Assem.instr list * allocation
 */
struct instr_list_and_allocation {
    assm_instr_t* ra_instrs;
    Table_T ra_allocation; // temp_t -> register (char*)
} ra_alloc(temp_state_t*, assm_instr_t*, ac_frame_t* frame);



#endif /* __REG_ALLOC_H__ */
