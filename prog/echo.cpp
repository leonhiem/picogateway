/**
 * prog/echo.cpp — echo <words...>: print its arguments, space-joined
 */
#include "kernel/prog.h"
#include <cstdio>

static int echo_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    // See prog/ls.cpp: snprintf()'s return value is how much it *would*
    // write, not how much it did -- must stop, not just count, once a
    // write would truncate, or pos overcounts past the real content.
    int pos = 0;
    for (int i = 0; i < argc; i++) {
        int avail = outlen - pos;
        int n = snprintf(out + pos, avail, i == 0 ? "%s" : " %s", argv[i]);
        if (n < 0 || n >= avail) break;
        pos += n;
    }
    if (pos < outlen) out[pos++] = '\n';
    return pos;
}

static const program_t prog_echo = {"echo", echo_run};

void echo_register(void)
{
    prog_register(&prog_echo);
}
