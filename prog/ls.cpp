/**
 * prog/ls.cpp — ls: list every registered device and program
 *
 * ls is a program too now, not a shell builtin -- it's a legitimate
 * text-out filter with no input, same shape as everything else. Devices
 * print as their own path (e.g. "/dev/lamp"); programs print with a
 * "bin/" prefix (e.g. "bin/cat") so the two namespaces stay visually
 * distinct in one combined listing.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>

static int ls_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    (void)argc;
    (void)argv;

    // snprintf() returns how many bytes it *would* write if the buffer
    // were big enough, not how many it actually wrote -- trusting that
    // return value to advance pos overcounts once the buffer runs low,
    // so later code (pipeline_run_once) prints past the real content
    // into whatever uninitialized memory follows it. n >= avail means
    // truncation happened: stop rather than count (or include) a
    // truncated entry.
    int pos = 0;
    int nd = fs_count();
    for (int i = 0; i < nd; i++) {
        int avail = outlen - pos;
        int n = snprintf(out + pos, avail, "%s\n", fs_name(i));
        if (n < 0 || n >= avail) break;
        pos += n;
    }
    int np = prog_count();
    for (int i = 0; i < np; i++) {
        int avail = outlen - pos;
        int n = snprintf(out + pos, avail, "bin/%s\n", prog_name(i));
        if (n < 0 || n >= avail) break;
        pos += n;
    }
    return pos;
}

static const program_t prog_ls = {"ls", ls_run};

void ls_register(void)
{
    prog_register(&prog_ls);
}
