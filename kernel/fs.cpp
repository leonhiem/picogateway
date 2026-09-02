#include "fs.h"
#include <cstdio>
#include <cstring>

static const device_t *devices[FS_MAX_DEVICES];
static int device_count = 0;

void fs_register(const device_t *dev)
{
    if (device_count >= FS_MAX_DEVICES) {
        // Loud on purpose: this exact silent-return used to drop
        // /dev/setpoint with no trace at all once the table filled up.
        // main() calls every *_register() before the shell starts, and
        // stdio_init_all() has already run by then, so this printf is
        // guaranteed to actually reach the console.
        printf("fs_register: table full (%d), dropped %s\n", FS_MAX_DEVICES, dev->name);
        return;
    }
    devices[device_count++] = dev;
}

static int find(const char *path)
{
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i]->name, path) == 0) return i;
    }
    return -1;
}

int fs_open(const char *path)
{
    int i = find(path);
    if (i < 0) return -1;
    if (devices[i]->open && devices[i]->open() != 0) return -1;
    return i; // the fd is just the device's table index -- no per-open
              // state needed for these devices, and nothing here opens
              // the same device concurrently from two places at once.
}

void fs_close(int fd)
{
    if (fd < 0 || fd >= device_count) return;
    if (devices[fd]->close) devices[fd]->close();
}

int fs_read(int fd, char *buf, int len)
{
    if (fd < 0 || fd >= device_count || !devices[fd]->read) return -1;
    return devices[fd]->read(buf, len);
}

int fs_write(int fd, const char *buf, int len)
{
    if (fd < 0 || fd >= device_count || !devices[fd]->write) return -1;
    return devices[fd]->write(buf, len);
}

int fs_count(void)
{
    return device_count;
}

const char *fs_name(int index)
{
    if (index < 0 || index >= device_count) return 0;
    return devices[index]->name;
}
