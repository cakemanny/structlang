#ifndef __ACTIVATION_H__
#define __ACTIVATION_H__
// vim:ft=c:
#include "symbols.h" // sl_sym_t
#include "ast.h" // sl_type_t
#include <stdint.h> // uint64_t
#include <stdbool.h> // bool
#include "target.h"
#include "temp.h"
#include "tree.h"
#include "interfaces/table.h"


typedef struct ac_frame {

    sl_sym_t acf_name; // function name
    int acf_last_local_offset;
    int acf_next_arg_offset;
    int acf_next_arg_reg; // temp
    size_t acf_outgoing_arg_bytes;
    tree_stm_t* acf_arg_moves;

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
        temp_t acf_spilled;
        temp_t acf_stored;

        struct ac_frame_var* acf_list;
    } *ac_frame_vars;

    struct ac_frame_var **ac_frame_vars_end; // for const-time append

    struct ac_frame* acf_link; // link all the frames in a list
} ac_frame_t;

void ac_frame_free(ac_frame_t** pframe);

/*
 * Returns the number of words used by the frame.
 * This can be added to the stack pointer on function entry to allocate the
 * right amount of space for the function.
 */
int ac_frame_words(const ac_frame_t* frame);

ac_frame_t* calculate_activation_records(
        const target_t*, temp_state_t*, sl_decl_t* program);
size_t size_of_type(const sl_decl_t* program, sl_type_t* type);
size_t alignment_of_type(const sl_decl_t* program, sl_type_t* type);

/*
 * Returns a record descriptor for the type. That is, a string indicating
 * whether each field is a pointer to another. A character for
 * each word
 * - p - means there is a word that holds a pointer
 * - n - means there is a word that holds no pointer (though could be multiple
 *   fields.)
 *
 * e.g.
 *   struct X { a: int, b: int, c: *int, d: bool }
 * would yield
 *   "npn"
 * The two ints, a and b, are 32-bit and fit into a single word which
 * is not a pointer, so the first character is 'n'. Then c is a pointer,
 * so the second character becomes 'p' and then d does not take a whole
 * word of space, but due to alignment criteria it uses up a non-pointer
 * word, and thus the final character is 'n'.
 *
 * The memory belongs to the caller and must be freed once the caller
 * is done with it.
 */
char* ac_record_descriptor_for_type(const sl_decl_t* program, sl_type_t* type);


typedef struct ac_frame_map_t {
    int acfm_num_arg_words;
    int acfm_num_local_words; // Total size of the space reserved for locals,
                              // including padding for alignment.
    int acfm_num_spill_words; // The length of prefix which contains spilled
                              // temps
    /*
     *  Array of the ids of the spilled registers. Not the same as the meaning
     *  in the final emitted frame map. 0 means empty - which abuses the
     *  knowledge that neither x0 nor rax are callee-save on arm64 and amd64
     *  respectively.
     */
    uint8_t acfm_spill_reg[10];
    uint64_t* acfm_args; // Bitmap of ptrs in arg space of frame
    uint64_t* acfm_locals; // Bitmap of ptrs in the space allocated for locals
    uint64_t* acfm_spills; // Bitmap indicating inherited ptr dispositions.
    ac_frame_t* acfm_frame; // The frame being mapped.
} ac_frame_map_t;

/*
 *
 */
ac_frame_map_t* ac_calculate_ptr_maps(ac_frame_t* frame, int* defined_vars);

extern const size_t ac_word_size;

extern bool ac_debug;

struct ac_frame_var* ac_spill_temporary(ac_frame_t* frame, temp_t t);

void ac_extend_frame_map_for_spills(
        ac_frame_map_t* frame_map, temp_list_t* spill_live_outs,
        Table_T allocation);

/*
 * Ensures that at least required_bytes have been added to the
 * activation record for function call arguments that are sent on
 * the stack. i.e. words 9 and above (caveats not considered).
 */
void reserve_outgoing_arg_space(ac_frame_t* frame, size_t required_bytes);

/*
 * adds instructions to the beginning of the function that move arguments
 * into their stack location (of reg location?)
 * and ergh, some other bits
 */
tree_stm_t* proc_entry_exit_1(
        temp_state_t* temp_state, ac_frame_t* frame, tree_stm_t* body);

#endif /* __ACTIVATION_H__ */
