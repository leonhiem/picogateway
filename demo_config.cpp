/**
 * demo_config.cpp — picogateway step 6: EEPROM config as /dev/config/*
 *
 * Brings up the real EEPROM (eeprom.h, carved out of gateway.cpp's
 * eeprom_write/eeprom_read) and registers /dev/config/wifi_ssid,
 * wifi_pass, url, farm, api0..api3 (dev/config.cpp) -- read/write,
 * validated on both sides (see dev/config.cpp's header comment).
 *
 * This is the same physical chip a real gateway.cpp unit has already
 * been writing to (same page/offset layout) -- expect cat to show
 * whatever farm name / wifi SSID / etc. are already stored there.
 *
 * Try it, over the serial console:
 *
 *   ls                          -- lists /dev/config/*, bin/*
 *   cat /dev/config/farm         -- whatever's actually on the chip
 *   echo NewFarm > /dev/config/farm
 *   cat /dev/config/farm         -- confirms it stuck
 *
 * gateway.cpp remains untouched; this is still scaffolding on its own
 * throwaway target, same as every step so far.
 */
#include <pico/stdlib.h>
#include "kernel/task.h"
#include "kernel/fs.h"
#include "kernel/prog.h"
#include "kernel/klog.h"
#include "jobs.h"
#include "eeprom.h"

extern void cat_register(void);
extern void echo_register(void);
extern void ls_register(void);
extern void task_shell(void);

static void task_console(void)
{
    klog_flush();
}

int main()
{
    stdio_init_all();
    sleep_ms(3000); // give the USB CDC console time to enumerate

    eeprom_init();
    config_register();

    cat_register();
    echo_register();
    ls_register();

    jobs_init();
    task_register("shell",   task_shell,    30); // 30ms: responsive to typing
    task_register("console", task_console, 100); // drains config_write's klog warnings

    while (1) {
        task_run();
        sleep_ms(1);
    }
}
