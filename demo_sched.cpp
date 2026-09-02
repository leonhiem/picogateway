/**
 * demo_sched.cpp — picogateway step 1
 *
 * Two independent tasks, different periods, proving the scheduler
 * (vendored verbatim from ../picoos/kernel/task.{h,cpp}) dispatches them
 * independently on this board, without touching any gateway hardware —
 * no LoRa, wifi, OLED, EEPROM, or buttons. Nothing here talks to
 * gateway.cpp, which keeps building unmodified alongside this target.
 *
 * Proof is over the USB serial console rather than an LED: watch the two
 * tick counts advance at different rates (~1 Hz vs ~1.7 Hz).
 */
#include <pico/stdlib.h>
#include <cstdio>
#include "kernel/task.h"

static void task_tick_a(void)
{
    static unsigned count = 0;
    printf("tick_a %u\n", ++count);
}

static void task_tick_b(void)
{
    static unsigned count = 0;
    printf("tick_b %u\n", ++count);
}

int main()
{
    stdio_init_all();
    sleep_ms(3000); // give the USB CDC console time to enumerate

    printf("picogateway demo_sched: scheduler proof, no hardware touched\n");

    task_register("tick_a", task_tick_a, 500); // 1 Hz
    task_register("tick_b", task_tick_b, 300); // ~1.7 Hz

    while (1) {
        task_run();
        sleep_ms(1); // scheduler is cooperative; this just caps the idle-spin rate
    }
}
