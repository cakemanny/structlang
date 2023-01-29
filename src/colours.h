#ifndef __COLOURS_H__
#define __COLOURS_H__

#include <stdbool.h>

extern struct term_colours {
    bool isatty;
    char* red;
    char* magenta;
    char* clear;
} term_colours;

#endif /* __COLOURS_H__ */
