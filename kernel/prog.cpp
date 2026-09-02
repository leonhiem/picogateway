#include "prog.h"
#include <cstdio>
#include <cstring>

static const program_t *progs[PROG_MAX];
static int count = 0;

void prog_register(const program_t *p)
{
    if (count >= PROG_MAX) {
        // Loud on purpose -- kernel/fs.h's fs_register() used to drop
        // devices silently past its own cap the same way; this is that
        // same fix applied here before it caused the same kind of bug.
        printf("prog_register: table full (%d), dropped %s\n", PROG_MAX, p->name);
        return;
    }
    progs[count++] = p;
}

const program_t *prog_find(const char *name)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(progs[i]->name, name) == 0) return progs[i];
    }
    return 0;
}

int prog_count(void)
{
    return count;
}

const char *prog_name(int index)
{
    if (index < 0 || index >= count) return 0;
    return progs[index]->name;
}
