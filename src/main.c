// vim:sw=4:
#include <stdlib.h> // exit
#include <stdio.h>
#include "colours.h"
#include "ast.h"
#include "semantics.h"
#include "rewrites.h"
#include "activation.h"
#include "temp.h"
#include "translate.h"

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
  -T    Stop after calculating activation records\n\
", stderr);
    exit(exit_code);
}

int main(int argc, char* argv[])
{
    _Bool parse_only = 0;
    _Bool stop_after_type_checking = 0;
    _Bool stop_after_rewrites = 0;
    _Bool stop_after_activation_calculation = 0;
    _Bool stop_after_translation = 0;
    _Bool warn_about_multiple_files = 0;
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
    sl_fragment_t* fragments =  translate_program(temp_state, program, frames);
    // ^ after this we can free up the ast structures
    if (!fragments) {
        fprintf(stderr, "internal error: failed to translate into trees\n");
        return 1;
    }
    if (stop_after_translation) {
        for (__auto_type frag = fragments; frag; frag = frag->fr_list) {
            tree_stm_print(stdout, frag->fr_body);
            fprintf(stdout, "\n");
        }
        return 0;
    }

    // end of program... maybe
    temp_state_free(&temp_state);
}
