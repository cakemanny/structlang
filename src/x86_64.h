#ifndef __X86_64_H__
#define __X86_64_H__
// vim:ft=c:

#include "activation.h" /* ac_frame_t */
#include "tree.h" /* tree_stm_t */
#include "assem.h" /* assm_instr_t */

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

#endif /* __X86_64_H__ */
