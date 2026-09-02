/**
 * dev/buttons.cpp — /dev/buttons: read-only, the 5 front-panel buttons
 *
 * Same shape as ../picoos's dev/buttons.cpp: cat /dev/buttons -> names of
 * whichever buttons were pressed since the last read (e.g. "up down\n",
 * or "none\n"), read-and-clear. Plus /dev/buttons/<name> siblings that
 * each only ever touch their own button's flag -- see picoos's header
 * comment (same file, same reasoning) for why the aggregate device
 * alone isn't enough for a consumer that only cares about one button.
 */
#include "buttons.h"
#include "kernel/fs.h"
#include <cstdio>

static const char *names[BUTTON_COUNT] = {"up", "down", "left", "right", "enter"};

static int buttons_read(char *buf, int len)
{
    int pos = 0;
    bool any = false;

    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (!button[i]) continue;
        int avail = len - pos;
        int n = snprintf(buf + pos, avail, "%s ", names[i]);
        if (n < 0 || n >= avail) break; // wouldn't fit -- stop before consuming it
        button[i] = false;
        any = true;
        pos += n;
    }

    if (!any) {
        return snprintf(buf, len, "none\n");
    }
    if (pos > 0 && pos <= len) buf[pos - 1] = '\n'; // swap trailing space
    return pos;
}

static const device_t dev_buttons = {
    "/dev/buttons",
    0,
    0,
    buttons_read,
    0, // read-only
};

void buttons_register(void)
{
    fs_register(&dev_buttons);
}

/* ═══════════════════════════════════════════════════
   /dev/buttons/<name> -- one independent read-and-clear device per
   button. See ../picoos/dev/buttons.cpp's header comment for why:
   the aggregate read() above drains *every* pending button at once,
   so a consumer watching just one button through it would silently
   steal the others' events too.
   ═══════════════════════════════════════════════════ */
static int one_read(int idx, char *buf, int len)
{
    bool pressed = button[idx];
    button[idx] = false;
    return snprintf(buf, len, pressed ? "1\n" : "0\n");
}

static int up_read(char *buf, int len)    { return one_read(BUTTON_UP, buf, len); }
static int down_read(char *buf, int len)  { return one_read(BUTTON_DOWN, buf, len); }
static int left_read(char *buf, int len)  { return one_read(BUTTON_LEFT, buf, len); }
static int right_read(char *buf, int len) { return one_read(BUTTON_RIGHT, buf, len); }
static int enter_read(char *buf, int len) { return one_read(BUTTON_ENTER, buf, len); }

static const device_t dev_button_up    = {"/dev/buttons/up",    0, 0, up_read,    0};
static const device_t dev_button_down  = {"/dev/buttons/down",  0, 0, down_read,  0};
static const device_t dev_button_left  = {"/dev/buttons/left",  0, 0, left_read,  0};
static const device_t dev_button_right = {"/dev/buttons/right", 0, 0, right_read, 0};
static const device_t dev_button_enter = {"/dev/buttons/enter", 0, 0, enter_read, 0};

void button_devices_register(void)
{
    fs_register(&dev_button_up);
    fs_register(&dev_button_down);
    fs_register(&dev_button_left);
    fs_register(&dev_button_right);
    fs_register(&dev_button_enter);
}
