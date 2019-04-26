#include <stdlib.h> // exit
#include <stdio.h>
#include "colours.h"
#include "ast.h"
#include "semantics.h"

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
", stderr);
    exit(exit_code);
}

int main(int argc, char* argv[])
{
    _Bool parse_only = 0;
    _Bool stop_after_type_checking = 0;
    _Bool warn_about_multiple_files = 0;
    char* inarg = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (char* pc = &argv[i][1]; *pc; pc++) {
                switch (*pc) {
                    case 'p': parse_only = 1; break;
                    case 't': stop_after_type_checking = 1; break;
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
}
