#ifndef __TRANSLATE_H__
#define __TRANSLATE_H__
// vim:ft=c:
#include "tree.h"
#include "ast.h" /* sl_decl_t */
#include "activation.h" /* ac_frame_t */
#include "fragment.h" /* sl_fragment_t */

/*
 * translate a program in our ast into the tree language IR
 *
 * returns a linked list of fragments, which each contain information
 * frame information and a function body - a statement in the tree IR
 */
sl_fragment_t* translate_program(
        temp_state_t* temp_state, sl_decl_t* program, ac_frame_t* frames);

#endif /* __TRANSLATE_H__ */
