#ifndef __SEMANTICS_H__
#define __SEMANTICS_H__

#include <stdbool.h>
#include "ast.h"
#include "interfaces/table.h"
#include "interfaces/arena.h"

int sem_verify_and_type_program(
        Arena_T arena, const char* filename, sl_decl_t* program);
bool sem_is_lvalue(const sl_expr_t* expr);

#endif /* __SEMANTICS_H__ */
