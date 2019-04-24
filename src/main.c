#include <stdio.h>
#include "ast.h"

// grammar.y
extern sl_decl_t* parse_file(const char* filename);

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: structc FILE\n");
        return 1;
    }

    char* inarg = argv[1];
    sl_decl_t* program = parse_file(inarg);

    // TODO: print program
}
