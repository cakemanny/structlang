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

#endif /* __X86_64_H__ */
