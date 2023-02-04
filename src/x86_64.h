#ifndef __X86_64_H__
#define __X86_64_H__
// vim:ft=c:

#include "activation.h" /* ac_frame_t */
#include "tree.h" /* tree_stm_t */
#include "assem.h" /* assm_instr_t */

// rbp is not included since it's a speccial reg (the frame pointer)
#define X86_68_CALLEE_SAVES { \
    {.temp_id = 3}, /* rbx */ \
    {.temp_id = 12}, /* r12 */ \
    {.temp_id = 13}, /* r13 */ \
    {.temp_id = 14}, /* r14 */ \
    {.temp_id = 15}, /* r15 */ \
}

/*
 * x86_64_codegen selects instructions for a single statement in the tree IR
 * language. It returns a list of instructions in the x86_64 architecture
 */
assm_instr_t* /* list */
x86_64_codegen(temp_state_t* temp_state, ac_frame_t* frame, tree_stm_t* stm);

/*
 * This sets our special registers e.g. the stack pointer and the callee-save
 * registers as live at the end of the function so the the register allocator
 * restores them before we exit
 */
assm_instr_t* /* list */
proc_entry_exit_2(ac_frame_t* frame, assm_instr_t* body);

/*
 * This adds a function prologue and epilogue.
 */
assm_fragment_t
proc_entry_exit_3(ac_frame_t* frame, assm_instr_t* body);

assm_instr_t* x86_64_load_temp(struct ac_frame_var* v, temp_t temp);

assm_instr_t* x86_64_store_temp(struct ac_frame_var* v, temp_t temp);


#endif /* __X86_64_H__ */
