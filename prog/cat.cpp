/**
 * prog/cat.cpp — cat <path>: read a device, or pass piped input through
 *
 * With a path argument, opens and reads that device (its usual job:
 * `cat /dev/skintemp`). With no argument, just echoes whatever was
 * piped in unchanged -- real Unix cat's other job, useful as a no-op
 * pipeline stage.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstring>

static int cat_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    if (argc >= 1) {
        int fd = fs_open(argv[0]);
        if (fd < 0) return -1;
        int n = fs_read(fd, out, outlen);
        fs_close(fd);
        return n;
    }
    int n = inlen < outlen ? inlen : outlen;
    memcpy(out, in, n);
    return n;
}

static const program_t prog_cat = {"cat", cat_run};

void cat_register(void)
{
    prog_register(&prog_cat);
}
