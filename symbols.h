#ifndef __SYMBOLS_H__
#define __SYMBOLS_H__

int Atom_length(const char* str);
const char* Atom_new(const char* str, int len);
const char* Atom_string(const char* str);
const char* Atom_int(long n);

typedef const char* sl_sym_t;

static sl_sym_t symbol(const char* str) {
    return Atom_string(str);
}

#endif /* __SYMBOLS_H__ */
