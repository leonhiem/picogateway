/**
 * dev/display.cpp — /dev/display/mode (read/write), /dev/display/status
 * (read-only)
 *
 * Same "up"/"down" convention as /dev/lora/status and /dev/wifi/status
 * for status; mode mirrors picoos's /dev/tft/mode exactly (read/write,
 * persistent, not a one-shot command) -- lets a script force the screen
 * back to the banner (`echo banner > /dev/display/mode`) without waiting
 * for a button press, or check which one is showing right now.
 */
#include "display.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

static int status_read(char *buf, int len)
{
    return snprintf(buf, len, display_is_ready() ? "up\n" : "down\n");
}

static int mode_read(char *buf, int len)
{
    return snprintf(buf, len, display_get_mode() == DISPLAY_MODE_MENU ? "menu\n" : "banner\n");
}

static int mode_write(const char *buf, int len)
{
    if (len >= 4 && strncmp(buf, "menu", 4) == 0)   { display_set_mode(DISPLAY_MODE_MENU);   return len; }
    if (len >= 6 && strncmp(buf, "banner", 6) == 0) { display_set_mode(DISPLAY_MODE_BANNER); return len; }
    return -1;
}

static const device_t dev_display_status = {"/dev/display/status", 0, 0, status_read, 0};
static const device_t dev_display_mode   = {"/dev/display/mode",   0, 0, mode_read,   mode_write};

void display_devices_register(void)
{
    fs_register(&dev_display_status);
    fs_register(&dev_display_mode);
}
