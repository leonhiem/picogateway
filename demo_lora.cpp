/**
 * demo_lora.cpp — picogateway step 7: LoRa field probes as /dev/lora/*
 *
 * Brings up the real LoRa radio (lora.cpp, carved out of gateway.cpp's
 * LoRa.begin()/parsePacket() loop) and registers
 * /dev/lora/probe{1,2,3}/{temp,soil,ph,rssi,age,seen} (dev/lora.cpp).
 * task_poll_lora runs every 50ms -- LoRa.parsePacket() is a quick
 * register read, not a blocking wait, so that's cheap, and it's what
 * makes packets get noticed promptly whenever a probe happens to
 * transmit.
 *
 * No picoos equivalent exists for this one -- first genuinely new
 * subsystem in this migration, not a carry-over of an existing
 * picoos pattern.
 *
 * Try it, over the serial console:
 *
 *   ls                          -- lists /dev/lora/probe1/temp etc., bin/*
 *   cat /dev/lora/probe1/seen    -- "0" until probe 1 actually transmits
 *   cat /dev/lora/probe1/temp    -- always a plain number: 0.0 until
 *                                   seen, then its last reading
 *   cat /dev/lora/probe1/age     -- seconds since that probe's last
 *                                   packet, -1 if never seen
 *
 * gateway.cpp remains untouched; this is still scaffolding on its own
 * throwaway target, same as every step so far.
 */
#include <pico/stdlib.h>
#include <cstdio>
#include "kernel/task.h"
#include "kernel/fs.h"
#include "kernel/prog.h"
#include "kernel/klog.h"
#include "jobs.h"
#include "lora.h"

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

    printf("picogateway demo_lora: starting LoRa radio...\n");
    lora_init();
    printf("LoRa started\n");
    lora_devices_register();

    cat_register();
    echo_register();
    ls_register();

    jobs_init();
    task_register("shell",     task_shell,     30); // 30ms: responsive to typing
    task_register("poll_lora", task_poll_lora, 50);  // see header comment re: interval
    task_register("console",   task_console,   100); // drains lora.cpp's radio-restart klog

    while (1) {
        task_run();
        sleep_ms(1);
    }
}
