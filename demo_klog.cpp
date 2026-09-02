/**
 * demo_klog.cpp — picogateway step 2: serialized logging across tasks
 *
 * Vendored from ../picoos's own klog demo, verbatim in shape: three
 * producer tasks log at different, deliberately unrelated rates; one
 * console task drains the queue. Proves klog() keeps output coherent —
 * one complete line at a time, in submission order — no matter how
 * independently the producers are scheduled. Still no gateway hardware
 * touched; gateway.cpp keeps building unmodified alongside this target.
 */
#include <pico/stdlib.h>
#include "kernel/task.h"
#include "kernel/klog.h"

static void task_sensor(void)
{
    static int n = 0;
    klog("sensor", "reading #%d", n++);
}

static void task_lora(void)
{
    static int n = 0;
    klog("lora", "poll #%d", n++);
}

static void task_ui(void)
{
    static int n = 0;
    klog("ui", "poll #%d", n++);
}

static void task_console(void)
{
    klog_flush();
}

int main()
{
    stdio_init_all();
    sleep_ms(3000); // give the USB CDC console time to enumerate

    task_register("sensor", task_sensor, 700);
    task_register("lora",   task_lora,   400);
    task_register("ui",     task_ui,     1300);
    task_register("console", task_console, 100);

    while (1) {
        task_run();
        sleep_ms(1);
    }
}
