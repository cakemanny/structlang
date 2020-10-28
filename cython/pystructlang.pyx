#cython: language_level=3
from ast import Name, Pointer, FuncT, Param, Struct, Func
from typing import List


cdef extern from "symbols.h":
    ctypedef const char* sl_sym_t


cdef extern from "ast.h":
    enum:
        SL_DECL_STRUCT = 1
        SL_DECL_FUNC = 2
        SL_DECL_PARAM = 3

    enum:
        SL_TYPE_NAME = 1
        SL_TYPE_PTR = 2
        SL_TYPE_ARRAY = 3
        SL_TYPE_FUNC = 4

    struct sl_decl_t:
        int dl_tag
        int dl_line
        sl_sym_t dl_name
        sl_decl_t* dl_params
        sl_type_t* dl_type
        # skip dl_body
        sl_decl_t* dl_list

    struct sl_type_t:
        int ty_tag
        sl_sym_t ty_name
        sl_type_t* ty_pointee


# defined in grammar.y
cdef extern sl_decl_t* parse_file(const char* filename);


def parse_and_convert(filename) -> List[Struct]:
    cdef list result = []
    cdef bytes bfname = filename.encode('utf-8')

    # call our c parse_file function
    cdef sl_decl_t* program = parse_file(bfname)

    cdef sl_decl_t* decl = program
    while decl is not NULL:
        if decl.dl_tag == SL_DECL_STRUCT:
            result.append(Struct(
                decl.dl_name.decode('utf-8'),
                convert_params(decl.dl_params),
                decl.dl_line
            ))
        elif decl.dl_tag == SL_DECL_FUNC:
            result.append(Func(
                decl.dl_name.decode('utf-8'),
                convert_params(decl.dl_params),
                convert_type(decl.dl_type),
                None, # body
                decl.dl_line,
            ))
        decl = decl.dl_list
    return result


cdef list convert_params(const sl_decl_t* dl_params):
    cdef list params = []

    cdef const sl_decl_t* arg = dl_params;
    while arg is not NULL:
        if arg.dl_tag == SL_DECL_PARAM:
            params.append(Param(
                arg.dl_name.decode('utf-8'),
                convert_type(arg.dl_type),
                arg.dl_line,
            ))
        else:
            print('unexpected tag:', arg.dl_tag)
        arg = arg.dl_list
    return params


# -> Type
cdef object convert_type(const sl_type_t* dl_type):
    assert dl_type != NULL
    if dl_type.ty_tag == SL_TYPE_NAME:
        return Name(dl_type.ty_name.decode('utf-8'))
    if dl_type.ty_tag == SL_TYPE_PTR:
        return Pointer(convert_type(dl_type.ty_pointee))
    if dl_type.ty_tag == SL_DECL_FUNC:
        return FuncT()
    return None
