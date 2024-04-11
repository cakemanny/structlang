#ifndef __REG_ALLOC_H__
#define __REG_ALLOC_H__
// vim:ft=c:

#include "assem.h" // assm_instr_t
#include "activation.h" // ac_frame_t
#include "interfaces/table.h"

/* type allocation = Table[temp_t, Frame.register] */

/**
 * val alloc : Assem.instr list * Frame.frame -> Assem.instr list * allocation
 */
struct instr_list_and_allocation {
    assm_instr_t* ra_instrs;
    Table_T ra_allocation; // temp_t -> register (char*)
};

struct instr_list_and_allocation
ra_alloc(FILE* out, temp_state_t*, assm_instr_t*, ac_frame_t* frame,
        bool print_interference_and_return,
        Table_T label_to_cs_bitmap, // sl_sym_t -> uint32_t
        Table_T label_to_spill_liveness, // sl_sym_t -> tmp_list_t*
        Arena_T arena_spill_liveness,
        Arena_T arena_instrs,
        Arena_T arena_allocation, // for the temp_t* s in ra_allocation
        Arena_T arena_fragments // for frame variables etc
        );



#endif /* __REG_ALLOC_H__ */
