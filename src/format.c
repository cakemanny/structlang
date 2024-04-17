#include "format.h"
#include "assertions.h"


void fprint_str_escaped(FILE* out, const char* str)
{
    const char* s = str;
    putc('\"', out);
    while (*s) {
        int c = *s;
        switch (c) {
            case '\\':
                putc('\\', out);
                putc(c, out);
                break;
            case '\"':
                fputs("\\\"", out);
                break;
            case '\n':
                fputs("\\n", out);
                break;
            case '\t':
                fputs("\\t", out);
                break;
            default:
                putc(c, out);
                break;
        }
        s++;
    }
    putc('\"', out);
}

size_t fmt_escaped_len(const char* str)
{
    /* we assume that most strings will be small, so processing them twice
       should not be too costly */

    // 2 for the opening and closing quotes and one for the terminating null
    size_t required = 3;

    for (const char* s = str; *s; s++)
    {
        int c = *s;
        if (c == '\\' || c == '\"' || c == '\n' || c == '\t') {
            required += 2;
        } else {
            required += 1;
        }
    }
    return required;
}

void fmt_snprint_escaped(char* out, size_t buf_len, const char* str)
{
    const char* s = str;
    char* dst = out;

    size_t required = fmt_escaped_len(str);
    assert(buf_len >= required);

    // TODO: deal with non-printable characters
    *dst++ = '\"';
    while (*s) {
        int c = *s;
        switch (c) {
            case '\\':
                *dst++ = '\\';
                *dst++ = c;
                break;
            case '\"':
                *dst++ = '\\';
                *dst++ = '"';
                break;
            case '\n':
                *dst++ = '\\';
                *dst++ = 'n';
                break;
            case '\t':
                *dst++ = '\\';
                *dst++ = 't';
                break;
            default:
                *dst++ = c;
                break;
        }
        s++;
    }
    *dst++ = '\"';
    *dst++ = '\0';
}
