/**
 * dev/room.cpp — /dev/room/temp, /dev/room/hum, /dev/room/light
 *
 * Read-only, same shape as picoos's dev/skintemp.cpp/dev/ambient.cpp:
 * cat /dev/room/temp -> "21.3\n". Unlike those two, this doesn't do a
 * live synchronous conversion on every read -- the DHT22 can't be
 * sampled that often (gateway.cpp only ever read it once every 2
 * minutes), so these just format whatever room.cpp's task_poll_room
 * last cached, same read-a-cache shape as /dev/buttons/<name>.
 */
#include "room.h"
#include "kernel/fs.h"
#include <cstdio>

static int temp_read(char *buf, int len)
{
    return snprintf(buf, len, "%.1f\n", room_temp_c);
}

static int hum_read(char *buf, int len)
{
    return snprintf(buf, len, "%.1f\n", room_hum_pct);
}

static int light_read(char *buf, int len)
{
    return snprintf(buf, len, "%.1f\n", room_light_pct);
}

static const device_t dev_room_temp  = {"/dev/room/temp",  0, 0, temp_read,  0};
static const device_t dev_room_hum   = {"/dev/room/hum",   0, 0, hum_read,   0};
static const device_t dev_room_light = {"/dev/room/light", 0, 0, light_read, 0};

void room_devices_register(void)
{
    fs_register(&dev_room_temp);
    fs_register(&dev_room_hum);
    fs_register(&dev_room_light);
}
