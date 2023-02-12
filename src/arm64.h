#ifndef __ARM64_H__
#define __ARM64_H__
// vim:ft=c:

#include "activation.h" /* ac_frame_t */
#include "tree.h" /* tree_stm_t */
#include "assem.h" /* assm_instr_t */

/*
 * arm64_codegen selects instructions for a single statement in the tree IR
 * language. It returns a list of instructions in the arm64 architecture
 */
assm_instr_t* /* list */
arm64_codegen(temp_state_t* temp_state, ac_frame_t* frame, tree_stm_t* stm);

#endif /* __ARM64_H__ */
