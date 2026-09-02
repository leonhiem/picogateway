/**
 * demo_fs.cpp — picogateway step 3: the device namespace, pointed at a
 * real device for the first time
 *
 * Brings up the 5 front-panel buttons for real (GPIO init + the actual
 * debounce timer, both carried over from gateway.cpp via buttons.cpp),
 * registers /dev/buttons + /dev/buttons/<name> (kernel/fs.h, dev/buttons.cpp),
 * and polls the aggregate device from a scheduled task -- proving
 * kernel/task + kernel/klog + kernel/fs + real hardware all work together
 * on this board. gateway.cpp remains untouched; this is the first
 * subsystem carved out of it, on its own throwaway target.
 *
 * Try it: press any button and watch the log line appear.
 */
#include <pico/stdlib.h>
#include <cstdio>
#include <cstring>
#include "kernel/task.h"
#include "kernel/klog.h"
#include "kernel/fs.h"
#include "buttons.h"

static int buttons_fd = -1;

static void task_poll_buttons(void)
{
    char buf[64];
    int n = fs_read(buttons_fd, buf, sizeof(buf));
    if (n <= 0) return;
    if (n < (int)sizeof(buf)) buf[n] = 0;
    if (buf[0] == '\0') return;
    // buf ends in '\n' already (buttons_read's contract) -- strip it so
    // klog's own trailing newline doesn't double up.
    for (char *p = buf; *p; p++) if (*p == '\n') { *p = 0; break; }
    if (strcmp(buf, "none") == 0) return;
    klog("buttons", "%s", buf);
}

static void task_console(void)
{
    klog_flush();
}

int main()
{
    stdio_init_all();
    sleep_ms(3000); // give the USB CDC console time to enumerate

    buttons_init();
    struct repeating_timer timer;
    add_repeating_timer_ms(-5, repeating_timer_callback, NULL, &timer);

    buttons_register();
    button_devices_register();
    buttons_fd = fs_open("/dev/buttons");

    printf("picogateway demo_fs: /dev/buttons live, press a button\n");

    task_register("poll_buttons", task_poll_buttons, 150);
    task_register("console", task_console, 100);

    while (1) {
        task_run();
        sleep_ms(1);
    }
}
