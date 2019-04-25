#include <stdlib.h> // exit
#include <stdio.h>
#include "ast.h"
#include "semantics.h"

// grammar.y
extern sl_decl_t* parse_file(const char* filename);

static void print_usage_and_exit(int exit_code)
{
    fprintf(stderr, "usage: structc [options] <input>\n");
    fputs("\
\n\
if '-' is given as an input, then stdin is read.\n\
\n\
options:\n\
  -p    Parse only\n\
  -t    Stop after type checking\n\
", stderr);
    exit(exit_code);
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        print_usage_and_exit(1);
    }

    char* inarg = argv[1];
    sl_decl_t* program = parse_file(inarg);

    for (sl_decl_t* decl = program; decl; decl = decl->dl_list) {
        dl_print(stdout, decl);
        fprintf(stdout, "\n");
    }

    int sem_result = sem_verify_and_type_program(inarg, program);
    if (sem_result < 0) {
        return 1;
    }
}
