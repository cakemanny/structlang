#include <stdlib.h> // exit
#include <sys/wait.h> // wait
#include <stdio.h> // perror
#include <string.h> // strdup
#include <unistd.h> // isatty
#include "colours.h"

// exported
struct term_colours term_colours;

#define pexit(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)

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

// initialise term_colours struct before main starts
static void init_term_colours() __attribute__((constructor));
void init_term_colours()
{
    term_colours.isatty = isatty(STDERR_FILENO);
    if (!term_colours.isatty) {
        term_colours.red = "";
        term_colours.magenta = "";
        term_colours.clear = "";
        return;
    }

    // shortcut for common case of xterm
    const char* term = getenv("TERM");
    if (term && strcmp(term, "xterm-256color") == 0) {
        term_colours.red = "\x1b[31m";
        term_colours.magenta = "\x1b[35m";
        term_colours.clear = "\x1b(B\x1b[m";
        return;
    }

    // slow path
    {
        char* tput_args[] = {"tput", "setaf", "1", NULL};
        read_term_colour(&term_colours.red, tput_args);
    }
    {
        char* tput_args[] = {"tput", "setaf", "5", NULL};
        read_term_colour(&term_colours.magenta, tput_args);
    }
    {
        char* tput_args[] = {"tput", "sgr0", NULL};
        read_term_colour(&term_colours.clear, tput_args);
    }
}
