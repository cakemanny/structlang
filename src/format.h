#ifndef __FORMAT_H__
#define __FORMAT_H__
// vim:ft=c:

#include <stdio.h> // FILE*

void _fprint_str_escaped(FILE* out, const char* str);
size_t fmt_escaped_len(const char* str);
void fmt_snprint_escaped(char* dst, size_t len, const char* str);

#endif // __FORMAT_H__
