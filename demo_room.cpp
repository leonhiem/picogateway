/**
 * demo_room.cpp — picogateway step 5: room sensors on the scheduler
 *
 * Brings up the real DHT22 (temp+hum) and light ADC (room.cpp, carved
 * out of gateway.cpp's main loop), registers /dev/room/temp,
 * /dev/room/hum, /dev/room/light (dev/room.cpp), and polls them from a
 * kernel/task.h task -- same architecture as demo_fs.cpp's buttons, but
 * this time the periodic work (task_poll_room) does the real hardware
 * read itself, since the DHT22 can't be sampled every scheduler tick.
 * The devices then just serve whatever it last cached.
 *
 * Poll interval here is 10s, not gateway.cpp's real 2-minute
 * SENSOR_INTERVAL_TIME -- shortened only for a fast feedback loop while
 * testing; the DHT22 datasheet only requires >=2s between reads, so
 * this is still safely within spec. Pick the real interval when this
 * gets wired into gateway.cpp for good.
 *
 * Try it, over the serial console:
 *
 *   ls                 -- lists /dev/room/temp, /dev/room/hum, /dev/room/light, bin/*
 *   cat /dev/room/temp -- last cached reading, updates every 10s
 *
 * gateway.cpp remains untouched; this is still scaffolding on its own
 * throwaway target, same as every step so far.
 */
#include <pico/stdlib.h>
#include "kernel/task.h"
#include "kernel/fs.h"
#include "kernel/prog.h"
#include "jobs.h"
#include "room.h"

extern void cat_register(void);
extern void echo_register(void);
extern void ls_register(void);
extern void task_shell(void);

int main()
{
    stdio_init_all();
    sleep_ms(3000); // give the USB CDC console time to enumerate

    room_init();
    room_devices_register();

    cat_register();
    echo_register();
    ls_register();

    jobs_init();
    task_register("shell",     task_shell,     30);    // 30ms: responsive to typing
    task_register("poll_room", task_poll_room, 10000);  // see header comment re: interval

    while (1) {
        task_run();
        sleep_ms(1);
    }
}
