#ifndef __SEMANTICS_H__
#define __SEMANTICS_H__

#include "ast.h"

typedef struct scope_t {
    struct scope_t* sc_parent;
} scope_t;

typedef struct sem_info_t {
    sl_decl_t* si_program;
    const char* si_filename;
    scope_t* si_root_scope;
} sem_info_t;

int sem_verify_and_type_program(const char* filename, sl_decl_t* program);

#endif /* __SEMANTICS_H__ */
