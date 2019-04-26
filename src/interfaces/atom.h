#ifndef __ATOM_H__
#define __ATOM_H__

int Atom_length(const char* str);
const char* Atom_new(const char* str, int len);
const char* Atom_string(const char* str);
const char* Atom_int(long n);

#endif /* __ATOM_H__ */
