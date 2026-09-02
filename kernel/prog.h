/**
 * kernel/prog.h — small named programs, parallel to kernel/fs.h's devices
 *
 * A program is a pure text-in/text-out filter: given whatever piped in
 * from the previous pipeline stage plus its own argv, produce output
 * text. No persistent state, no open/close -- unlike devices, programs
 * don't own hardware, they just transform text. Registered the same
 * way devices are, and listed alongside them by the "ls" program
 * itself (prog/ls.cpp) so the shell's `ls` shows both in one place.
 */
#pragma once

#define PROG_MAX 24 // 2026-08-20: bumped from 12 -- phase/safelut/select
                    // would have pushed count to 13, past the old cap,
                    // through the exact same silent-drop TODO fs_register
                    // had before it got a loud warning (see prog.cpp).
                    // Given headroom this time rather than sized to match.

typedef struct {
    const char *name;
    // in/inlen: piped input from the previous stage (or a `<` redirect),
    //   empty if this is the first stage with neither.
    // argc/argv: words following the program name on the command line.
    // out/outlen: buffer for the result; return bytes written, or -1
    //   on error (bad args, device not found, etc).
    int (*run)(const char *in, int inlen, int argc, char **argv, char *out, int outlen);
} program_t;

void prog_register(const program_t *prog);
const program_t *prog_find(const char *name);
int prog_count(void);
const char *prog_name(int index);
