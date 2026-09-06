/**
 * prog/display.cpp — display: bring up the OLED
 *
 * Same shape as prog/lora.cpp/prog/wifi.cpp: gateway.cpp's main() never
 * touches the display's I2C bus -- display_try_start() (display.cpp)
 * only ever runs from here, one non-blocking, rate-limited attempt per
 * call. Meant to run backgrounded from the boot script:
 *
 *   display &
 *
 * task_display() (registered unconditionally in gateway.cpp, like
 * task_poll_lora) no-ops until this has actually succeeded, so nothing
 * draws to the panel before the boot script says to.
 */
#include "kernel/prog.h"
#include "display.h"
#include <cstdio>

static int display_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    (void)argc;
    (void)argv;

    bool up = display_try_start();
    return snprintf(out, outlen, up ? "ready\n" : "starting\n");
}

static const program_t prog_display = {"display", display_run};

void display_register(void)
{
    prog_register(&prog_display);
}
