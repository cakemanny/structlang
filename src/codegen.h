#ifndef __CODEGEN_H__
#define __CODEGEN_H__
// vim:ft=c:
#include "activation.h"
#include "assem.h"
#include "fragment.h"
#include <stdio.h>

typedef struct codegen_t {
    /*
     * codegen selects instructions for a single statement in the tree IR
     * language. It returns a list of instructions in the target architecture
     */
    assm_instr_t* /* list */
    (*codegen)(Arena_T, temp_state_t* temp_state, sl_fragment_t* fragment, tree_stm_t* stm);

    /*
     * This sets our special registers e.g. the stack pointer and the callee-save
     * registers as live at the end of the function so the the register allocator
     * restores them before we exit.
     * The arena to pass is the one passed to codegen
     */
    assm_instr_t* /* list */
    (*proc_entry_exit_2)(ac_frame_t* frame, assm_instr_t* body, Arena_T);

    /*
     * This adds a function prologue and epilogue.
     */
    assm_fragment_t
    (*proc_entry_exit_3)(ac_frame_t* frame, assm_instr_t* body, Arena_T);

    assm_instr_t* (*load_temp)(struct ac_frame_var*, temp_t, Arena_T);
    assm_instr_t* (*store_temp)(struct ac_frame_var*, temp_t, Arena_T);


    void (*emit_text_segment_header)(FILE* out);

    void (*emit_data_segment)(
            FILE* out,
            const sl_fragment_t* fragments,
            Table_T label_to_cs_bitmap);

} codegen_t;

#endif /* __CODEGEN_H__ */
