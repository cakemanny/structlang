#ifndef __ACTIVATION_H__
#define __ACTIVATION_H__
// vim:ft=c:
#include "symbols.h" // sl_sym_t
#include "ast.h" // sl_type_t
#include <stdint.h> // uint64_t
#include <stdbool.h> // bool
#include "target.h"
#include "temp.h"
#include "target.h"
#include "tree.h"
#include "interfaces/table.h"
#include "assem.h"


typedef struct ac_frame {

    sl_sym_t acf_name; // function name
    int acf_last_local_offset; // temp
    int acf_next_arg_offset; // temp
    uint64_t *acf_locals_ptr_bitset;
    uint64_t *acf_args_ptr_bitset;
    int acf_next_arg_reg; // temp

    const target_t* acf_target;

    // The mapping from temp_t* to register names
    Table_T acf_temp_map;

    struct ac_frame_var {
        enum {
            ACF_ACCESS_FRAME = 1,
            ACF_ACCESS_REG,
        } acf_tag;

        sl_sym_t acf_varname; // this is not a unique identifier
        size_t acf_size;
        size_t acf_alignment;
        int acf_var_id;
        union {
            int acf_offset;         // ACF_ACCESS_FRAME
            temp_t acf_reg;         // ACF_ACCESS_REG
        };
        //bool acf_escapes; //?
        bool acf_is_formal; // i.e. function parameter
        uint64_t* acf_ptr_map; // bitset

        struct ac_frame_var* acf_list;
    } *ac_frame_vars;

    struct ac_frame_var **ac_frame_vars_end; // for const-time append

    struct ac_frame* acf_link; // link all the frames in a list
} ac_frame_t;

void ac_frame_free(ac_frame_t** pframe);

ac_frame_t* calculate_activation_records(enum target_type, sl_decl_t* program);
size_t size_of_type(const sl_decl_t* program, sl_type_t* type);
size_t alignment_of_type(const sl_decl_t* program, sl_type_t* type);

extern const size_t ac_word_size;

extern bool ac_debug;

struct ac_frame_var* ac_spill_temporary(ac_frame_t* frame);

/*
 * adds instructions to the beginning of the function that move arguments
 * into their stack location (of reg location?)
 * and ergh, some other bits
 */
tree_stm_t* proc_entry_exit_1(
        temp_state_t* temp_state, ac_frame_t* frame, tree_stm_t* body);

#endif /* __ACTIVATION_H__ */
