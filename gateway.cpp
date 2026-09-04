/**
 * gateway.cpp — main(): the real firmware, now on the multitasking core
 *
 * Step 9 of the picoos-style migration (see README / the
 * picoos-style-migration-goal memory note): gateway.cpp's old
 * hand-rolled main loop -- OLED menu/animation, the blocking wifi
 * connect, the ThingSpeak HTTP client, the raw LoRa receive loop, all
 * running serially in one giant while(1) -- is gone from this file.
 * None of it is lost: it's in git history (every commit through step
 * 8), and it comes back piece by piece as devices/bin programs on top
 * of kernel/fs.h, the same way picoos was rebuilt from babywarmer.
 *
 * What starts unconditionally at boot, right here, is exactly the
 * "reliable" local hardware: buttons, room sensors (DHT22 + light,
 * room.cpp), and the config EEPROM (eeprom.cpp/dev/config.cpp). None
 * of it has external state to lose, so none of it needs to be
 * restartable -- it just comes up and stays up.
 *
 * LoRa's poll task is registered too, but stays idle: task_poll_lora()
 * no-ops until lora_init() has actually run (see lora.cpp's
 * radio_ready flag), and nothing here calls lora_init(). Starting the
 * radio -- and restarting it if the connection ever needs it -- is
 * bin/lora's job (step 10), run from the boot script (shell.cpp's
 * BOOT_SCRIPT_TEXT) so it's a normal, killable/re-runnable background
 * job instead of a blocking call sitting in main(). Wifi (step 11)
 * and the OLED display (later) follow the same pattern.
 *
 * The watchdog gateway.cpp used to drive (8s timeout, fed from deep
 * inside the wifi/http loop) isn't re-enabled here on purpose -- it
 * existed mainly to recover from wifi/http hangs that no longer exist
 * in this file, and turning it on without anything feeding it from
 * the new architecture yet would just reboot the board every 8s.
 * Revisit once bin/wifi (step 11) exists.
 */
#include <pico/stdlib.h>
#include <cstdio>
#include "hardware/watchdog.h"
#include "kernel/task.h"
#include "kernel/fs.h"
#include "kernel/prog.h"
#include "kernel/klog.h"
#include "jobs.h"
#include "buttons.h"
#include "room.h"
#include "eeprom.h"
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

    if (watchdog_caused_reboot()) {
        printf("Rebooted by watchdog\n");
    } else {
        printf("Clean boot\n");
    }
    printf("picogateway starting...\n");

    buttons_init();
    room_init();
    eeprom_init();

    struct repeating_timer button_timer;
    add_repeating_timer_ms(-5, repeating_timer_callback, NULL, &button_timer);

    buttons_register();
    button_devices_register();
    room_devices_register();
    config_register();
    lora_devices_register(); // reads as 0.0/never-seen until bin/lora (step 10) starts the radio

    cat_register();
    echo_register();
    ls_register();

    jobs_init();
    task_register("shell",     task_shell,     30);    // 30ms: responsive to typing
    task_register("poll_room", task_poll_room, 10000);  // see room.h -- 10s for now
    task_register("poll_lora", task_poll_lora, 50);     // no-ops until the radio's started -- see lora.cpp
    task_register("console",   task_console,   100);    // drains klog (config_write warnings, lora auto-repair)

    while (1) {
        task_run();
        sleep_ms(1);
    }
}
