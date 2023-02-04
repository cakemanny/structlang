// vim:sw=4:
#include <stdlib.h> // exit
#include <stdio.h>
#include <stdbool.h>
#include "colours.h"
#include "ast.h"
#include "semantics.h"
#include "rewrites.h"
#include "activation.h"
#include "temp.h"
#include "translate.h"
#include "canonical.h"
#include "x86_64.h"
#include "liveness.h"
#include "reg_alloc.h"

#define var __auto_type

// grammar.y
extern sl_decl_t* parse_file(const char* filename);

static void print_usage_and_exit(int exit_code) __attribute__((noreturn));
static void print_usage_and_exit(int exit_code)
{
    fprintf(stderr, "usage: structc [options] <input>\n");
    fputs("\
\n\
if '-' is given as an input, then stdin is read.\n\
\n\
options:\n\
  -p    Parse only (print ast)\n\
  -t    Stop after type checking\n\
  -r    Stop after rewrites and print ast\n\
  -a    Stop after calculating activation records\n\
  -T    Stop after translating into the tree IR\n\
  -C    Stop after canonicalising the tree IR\n\
  -i    Stop after instruction selection\n\
  -l    Stop after liveness analysis\n\
", stderr);
    exit(exit_code);
}

int main(int argc, char* argv[])
{
    bool parse_only = 0;
    bool stop_after_type_checking = 0;
    bool stop_after_rewrites = 0;
    bool stop_after_activation_calculation = 0;
    bool stop_after_translation = 0;
    bool stop_after_canonicalisation = 0;
    bool stop_after_instruction_selection = 0;
    bool warn_about_multiple_files = 0;
    bool stop_after_liveness_analysis = 0;
    char* inarg = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (char* pc = &argv[i][1]; *pc; pc++) {
                switch (*pc) {
                    case 'p': parse_only = 1; break;
                    case 't': stop_after_type_checking = 1; break;
                    case 'r': stop_after_rewrites = 1; break;
                    case 'a': stop_after_activation_calculation = 1; break;
                    case 'T': stop_after_translation = 1; break;
                    case 'C': stop_after_canonicalisation = 1; break;
                    case 'i': stop_after_instruction_selection = 1; break;
                    case 'l': stop_after_liveness_analysis = 1; break;
                    default: fprintf(stderr, "unknown option '%c'\n", *pc);
                             print_usage_and_exit(1);
                }
            }
        } else if (!inarg) {
            inarg = argv[i];
        } else {
            warn_about_multiple_files = 1;
        }
    }
    if (inarg == NULL) {
        print_usage_and_exit(1);
    }
    if (warn_about_multiple_files) {
        fprintf(stderr, "%swarning:%s only %s will be considered for input\n",
                term_colours.magenta, term_colours.clear, inarg);
    }

    sl_decl_t* program = parse_file(inarg);
    if (!program) {
        return 1;
    }

    if (parse_only) {
        for (sl_decl_t* decl = program; decl; decl = decl->dl_list) {
            dl_print(stdout, decl);
            fprintf(stdout, "\n");
        }
        return 0;
    }

    int sem_result = sem_verify_and_type_program(inarg, program);
    if (sem_result < 0) {
        fprintf(stderr, "%d errors\n", -sem_result);
        return 1;
    }

    if (stop_after_type_checking) {
        // Print typed tree?
        return 0;
    }

    // make some small transformations that make it easier to transform the
    // program into the lower level language
    rewrite_decompose_equal(program);

    if (stop_after_rewrites) {
        for (sl_decl_t* decl = program; decl; decl = decl->dl_list) {
            dl_print(stdout, decl);
            fprintf(stdout, "\n");
        }
        return 0;
    }

    ac_frame_t* frames = calculate_activation_records(program);
    if(!frames) {
        // TODO: consider a module with only struct definitions?
        fprintf(stderr, "internal error: failed to calculate frames\n");
        return 1;
    }

    if (stop_after_activation_calculation) {
        return 0;
    }

    temp_state_t* temp_state = temp_state_new();
    sl_fragment_t* fragments = translate_program(temp_state, program, frames);
    // ^ after this we can free up the ast structures
    if (!fragments) {
        fprintf(stderr, "internal error: failed to translate into trees\n");
        return 1;
    }
    if (stop_after_translation) {
        for (var frag = fragments; frag; frag = frag->fr_list) {
            fprintf(stdout, "# %s\n", frag->fr_frame->acf_name);
            tree_printf(stdout, "%S\n", frag->fr_body);
        }
        return 0;
    }

    sl_fragment_t* canonical_frags = canonicalise_tree(temp_state, fragments);
    if(!canonical_frags) {
        fprintf(stderr, "internal error: failed to canonicalise trees\n");
        return 1;
    }
    if (stop_after_canonicalisation) {
        for (var frag = fragments; frag; frag = frag->fr_list) {
            fprintf(stdout, "# %s\n", frag->fr_frame->acf_name);
            for (var s = frag->fr_body; s; s = s->tst_list) {
                tree_printf(stdout, "%S\n", s);
            }
            fprintf(stdout, "\n");
        }
        return 0;
    }

    for (var frag = fragments; frag; frag = frag->fr_list) {
        assm_instr_t* body_instrs = NULL;
        fprintf(stdout, "# %s\n", frag->fr_frame->acf_name); // TODO: remove
        for (var s = frag->fr_body; s; s = s->tst_list) {

            if (stop_after_instruction_selection) {
                tree_printf(stdout, "## %S\n", s);
            }

            assm_instr_t* instrs = x86_64_codegen(temp_state, frag->fr_frame, s);
            if (stop_after_instruction_selection) {
                for (var i = instrs; i; i = i->ai_list) {
                    char buf[128];
                    assm_format(buf, 128, i, frag->fr_frame->acf_temp_map);
                    fprintf(stdout, "%s", buf);
                }
            }
            body_instrs = assm_list_chain(body_instrs, instrs);
        }
        if (stop_after_instruction_selection) {
            fprintf(stdout, "\n");
            continue;
        }
        body_instrs = proc_entry_exit_2(frag->fr_frame, body_instrs);

        var instrs_and_allocation =
            ra_alloc(temp_state, body_instrs, frag->fr_frame,
                    stop_after_liveness_analysis);
        if (stop_after_liveness_analysis) {
            break;
        }

        var final_fragment = proc_entry_exit_3(
                frag->fr_frame, instrs_and_allocation.ra_instrs);

        fputs(final_fragment.asf_prologue, stdout);
        for (var i = final_fragment.asf_instrs; i; i = i->ai_list) {
            // FIXME: we need to provide a separate allocation for each
            // instruction size
            char buf[128];
            assm_format(buf, 128, i, instrs_and_allocation.ra_allocation);
            fprintf(stdout, "%s", buf);
        }
        fputs(final_fragment.asf_epilogue, stdout);
    }

    // end of program... maybe
    temp_state_free(&temp_state);
}
