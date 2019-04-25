#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h> // isatty
#include "semantics.h"

#define pexit(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)

static struct {
    _Bool initialized;
    _Bool isatty;
    char* red;
    char* clear;
} term_colours;

static void read_term_colour(char** pcolour, char* tput_args[])
{
    int link[2] = {};
    char buffer[16] = {};

    if (pipe(link) == -1) {
        pexit("pipe");
    }

    pid_t pid = fork();
    if (pid == -1) {
        pexit("fork");
    }
    if (pid == 0) {
        dup2(link[1], STDOUT_FILENO);
        close(link[0]);
        close(link[1]);

        execv("/usr/bin/tput", tput_args);
        pexit("execv /usr/bin/tput");
    }

    close(link[1]);
    ssize_t nbytes = read(link[0], buffer, sizeof buffer - 1);
    if (nbytes == -1) {
        pexit("read");
    }
    *pcolour = strdup(buffer);

    wait(NULL);
}

static void init_term_colours()
{
    term_colours.initialized = 1;
    term_colours.isatty = isatty(fileno(stderr));
    if (term_colours.isatty) {
        term_colours.red = "";
        term_colours.clear = "";
    }

    {
        char* tput_args[] = {"tput", "setaf", "1", NULL};
        char** target = &term_colours.red;
        read_term_colour(target, tput_args);
    }
    {
        char* tput_args[] = {"tput", "sgr0", NULL};
        char** target = &term_colours.clear;
        read_term_colour(target, tput_args);
    }
}

#define elprintf(fmt, fn, lineno, ...) do { \
    /*_Static_assert((typeof(lineno) == int), "lineno must be int");*/ \
    if (!term_colours.initialized) {init_term_colours();} \
    fprintf(stderr, "%s:%d: %serror:%s " fmt, fn, lineno, \
            term_colours.red, term_colours.clear , ##__VA_ARGS__); \
} while (0)

static _Bool lookup_type_decl(sem_info_t* sem_info, sl_type_t* type)
{
    sl_type_t* t = type;
    while (t->ty_tag != SL_TYPE_NAME) {
        assert(t->ty_tag == SL_TYPE_PTR || t->ty_tag == SL_TYPE_ARRAY);
        t = t->ty_pointee;
    }
    if (t->ty_name == symbol("int")) {
        return 1;
    }

    sl_decl_t* x;
    for (x = sem_info->si_program; x; x = x->dl_list) {
        if (x->dl_tag == SL_DECL_STRUCT) {
            if (x->dl_name == t->ty_name) {
                return 1;
            }
        }
    }
    return 0;
}

static int verify_decl_struct(sem_info_t* sem_info, sl_decl_t* decl)
{
    int result = 0;
    for (sl_decl_t* field = decl->dl_params; field;
            field = field->dl_list) {
        // check type is known
        if (!lookup_type_decl(sem_info, field->dl_type)) {
            elprintf("unknown type: '", sem_info->si_filename, field->dl_line);
            ty_print(stderr, field->dl_type);
            fprintf(stderr, "'\n");
            result = -1;
        }

        // check names aren't used twice
        for (sl_decl_t* field2 = field->dl_list; field2;
                field2 = field2->dl_list) {
            if (field->dl_name == field2->dl_name) {
                elprintf("second declaration of field '%s' in struct '%s'\n",
                        sem_info->si_filename, field2->dl_line,
                        field->dl_name, decl->dl_name);
                result = -1;
            }
        }
    }
    return result;
}

static int verify_decl_func(sem_info_t* sem_info, sl_decl_t* decl)
{
    int result = 0;
    // check parameters, types known, no duplicates
    // create new scope
    // push parameters into scope

    // check each expression
    //
    return result;
}

static int verify_decl(sem_info_t* sem_info, sl_decl_t* decl)
{
    int result = 0;
    switch (decl->dl_tag) {
        case SL_DECL_STRUCT:
            return verify_decl_struct(sem_info, decl);
        case SL_DECL_FUNC:
            return verify_decl_func(sem_info, decl);
        case SL_DECL_PARAM:
            return result;
    }
    assert(0 && "verify_decl missing case");
}

int sem_verify_and_type_program(const char* filename, sl_decl_t* program)
{
    int result = 0;
    sem_info_t sem_info = {
        .si_program = program,
        .si_filename = (strcmp(filename, "-") == 0) ? "<stdin>" : filename,
        .si_root_scope = NULL,
    };

    // Add names to root scope
    for (const sl_decl_t* decl = program; decl; decl = decl->dl_list) {
        // Is this necessary?
        for (const sl_decl_t* decl2 = decl->dl_list; decl2;
                decl2 = decl2->dl_list) {
            if (decl->dl_name == decl2->dl_name) {
                result = -1;
            }
        }
    }

    if (result < 0) {
        return result;
    }

    for (sl_decl_t* decl = program; decl; decl = decl->dl_list) {
        result += verify_decl(&sem_info, decl);
    }
    return result;
}
