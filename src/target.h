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
    Table_T // temp_t* -> const char*
        (*tgt_temp_map)(); // returns table mapping temps to their register names

    struct codegen_t* tgt_backend;
} target_t;


extern const target_t target_arm64;
extern const target_t target_x86_64;

#if defined(__arm64__)
#  define TARGET_DEFAULT target_arm64
#elif defined(__x86_64__)
#  define TARGET_DEFAULT target_x86_64
#else
#  warning "No target for platform: defaulting to arm64"
#  define TARGET_DEFAULT target_arm64
#endif

#endif /* __TARGET_H__ */
