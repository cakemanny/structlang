#ifndef __TARGET_H__
#define __TARGET_H__
// vim:ft=c:
#include <stddef.h> // size_t
#include "interfaces/table.h"
#include "temp.h"

enum target_type {
    TARGET_X86_64 = 1,
    TARGET_ARM64,
};

struct codegen_t;

typedef struct temp_array_t {
    size_t length;
    const temp_t* elems;
} temp_array_t;

/*
 * target_t contains useful details of the machine for earlier stages
 * of the compiler.
 */
typedef struct target_t {
    size_t word_size;
    size_t stack_alignment;
    temp_array_t arg_registers;
    temp_t tgt_sp;
    temp_t tgt_fp;
    temp_t tgt_ret0;
    temp_t tgt_ret1;
    const temp_array_t callee_saves;
    const char** register_names;
    const char* (*register_for_size)(const char* regname, size_t size);

    struct codegen_t* tgt_backend;
} target_t;


extern const target_t target_arm64;
extern const target_t target_x86_64;

Table_T x86_64_temp_map();
Table_T arm64_temp_map();

#endif /* __TARGET_H__ */
