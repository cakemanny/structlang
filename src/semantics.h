#ifndef __SEMANTICS_H__
#define __SEMANTICS_H__

#include "ast.h"
#include "interfaces/table.h"

typedef struct scope_t {
    Table_T sc_bindings;
    struct scope_t* sc_parent;
} scope_t;

typedef struct scope_entry_t {
    sl_type_t* sce_type;
    int sce_var_id;
} scope_entry_t;

typedef struct sem_info_t {
    sl_decl_t*  si_program;
    const char* si_filename;
    scope_t*    si_root_scope;
    scope_t*    si_current_scope;
    sl_decl_t*  si_current_fn;
    int         si_loop_depth;
    int         si_next_var_id;
    struct {
        sl_type_t* int_type;
        sl_type_t* bool_type;
        sl_type_t* void_type;
    } si_builtin_types;
} sem_info_t;

int sem_verify_and_type_program(const char* filename, sl_decl_t* program);

#endif /* __SEMANTICS_H__ */
