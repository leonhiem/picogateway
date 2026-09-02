/**
 * demo_shell.cpp — picogateway step 4: the interactive shell
 *
 * Brings up the same real /dev/buttons as demo_fs.cpp, plus cat/echo/ls
 * (kernel/prog.h) and the pipe/redirect/background-job shell
 * (jobs.{h,cpp}, shell.cpp) on top of it. Try, over the serial console:
 *
 *   ls                    -- lists /dev/buttons(/*) and bin/cat,echo,ls
 *   cat /dev/buttons       -- one read, foreground
 *   cat /dev/buttons > /dev/buttons &   -- silly but harmless: proves &
 *   echo hello              -- no device needed at all
 *
 * gateway.cpp remains untouched; this is still scaffolding on its own
 * throwaway target, same as every step so far.
 */
#include <pico/stdlib.h>
#include "kernel/task.h"
#include "kernel/fs.h"
#include "kernel/prog.h"
#include "jobs.h"
#include "buttons.h"

extern void cat_register(void);
extern void echo_register(void);
extern void ls_register(void);
extern void task_shell(void);

int main()
{
    stdio_init_all();
    sleep_ms(3000); // give the USB CDC console time to enumerate

    buttons_init();
    struct repeating_timer timer;
    add_repeating_timer_ms(-5, repeating_timer_callback, NULL, &timer);

    buttons_register();
    button_devices_register();

    cat_register();
    echo_register();
    ls_register();

    jobs_init();
    task_register("shell", task_shell, 30); // 30ms: responsive to typing

    while (1) {
        task_run();
        sleep_ms(1);
    }
}
