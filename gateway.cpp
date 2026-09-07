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
 * LoRa's poll task is registered too, but stays idle at boot:
 * task_poll_lora() no-ops until the radio's actually up (lora.cpp's
 * radio_ready flag), and nothing in main() itself brings the radio up.
 * That's bin/lora's job (step 10, prog/lora.cpp) -- started from the
 * boot script (shell.cpp's BOOT_SCRIPT_TEXT: "lora &") as a normal,
 * killable/re-runnable background job instead of a blocking call
 * sitting in main(). If task_poll_lora() ever sees an implausible
 * packet, it clears radio_ready instead of blocking to fix it in
 * place; bin/lora's job notices and restarts the radio on its own next
 * tick.
 *
 * Wifi (step 11, wifi.cpp/prog/wifi.cpp) follows the same pattern:
 * nothing in main() brings the cyw43 chip up or connects it -- that's
 * bin/wifi's job, also started from the boot script ("wifi &"). Unlike
 * LoRa (this board is the one always-listening radio "master"), wifi
 * is a client, and association is a real multi-second operation that
 * can fail for reasons LoRa's SPI check never has to (bad password, AP
 * out of range) -- so /dev/wifi/status (dev/wifi.cpp) reports one of
 * three states, not two: down / connecting / up. A dropped link goes
 * straight back to down and bin/wifi's own next tick starts retrying,
 * same "no blocking retry loop, no separate watchdog needed for this"
 * shape as bin/lora. The OLED display (later) follows the same
 * pattern too.
 *
 * Step 12 (display.cpp/prog/display.cpp) follows the same pattern again:
 * nothing in main() touches the OLED's I2C bus -- that's bin/display's
 * job ("display &"). task_display() is registered unconditionally like
 * task_poll_lora/wifi, and no-ops until display_try_start() has actually
 * succeeded (display.cpp's own ready flag). The old animation()/
 * animation_wifi() menu code is gone from here the same way the old
 * wifi/http code was in step 9 -- rebuilt on /dev/room, /dev/lora,
 * /dev/wifi, /dev/config instead of a hand-rolled apis[4][22] array (see
 * display.cpp's own header comment for how that retires the apis[-1]
 * bug by construction, not just by coincidence).
 *
 * Step 13 finishes that promise: the OLED menu's Config branch
 * (display.cpp) can now edit all 8 EEPROM fields (wifi_ssid, wifi_pass,
 * url, farm, api0..3) too, each through fs_open/fs_read/fs_write on its
 * /dev/config/* device rather than any array indexed by a menu constant
 * -- no apis[4][22]-shaped buffer exists anywhere in this codebase any
 * more, so there's no equivalent off-by-one left to have. Nothing here
 * in gateway.cpp changed for step 13; it's entirely inside display.cpp.
 *
 * Step 14 (thingspeak.cpp/dev/thingspeak.cpp) replaces the old
 * run_tcp_client()/TCP_CLIENT_T entirely: the room sensors upload to
 * ThingSpeak every 60s, one LoRa probe's cached values round-robin
 * every 60s offset 30s from that, all through /dev/config/{url,api0..3}
 * for the channel/keys and plain lwIP tcp_* / dns_gethostbyname()
 * callbacks for the HTTP GET itself -- no blocking sleep_ms() loop, no
 * heap-allocated per-request state, unlike the original. There's no
 * bin/thingspeak: unlike lora/wifi/display, there's no hardware to
 * bring up here, just a wifi-up check task_thingspeak() makes on its
 * own every tick, so it's registered unconditionally like
 * task_poll_room.
 *
 * The watchdog gateway.cpp used to drive (8s timeout, fed from deep
 * inside the wifi/http loop) still isn't re-enabled here on purpose:
 * that loop is exactly what step 14 replaced, and its replacement can't
 * hang the scheduler the way the original could, so there's nothing left
 * for a watchdog to recover this file *from*. thingspeak.cpp's own
 * tcp_poll() backstop (a few seconds of silence fails the upload, not
 * the board) is this architecture's equivalent -- the same "the thing
 * that can get stuck heals itself" shape as bin/lora and bin/wifi.
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
#include "wifi.h"
#include "display.h"
#include "thingspeak.h"

extern void cat_register(void);
extern void echo_register(void);
extern void ls_register(void);
extern void lora_register(void);
extern void wifi_register(void);
extern void display_register(void);
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
    wifi_devices_register(); // /dev/wifi/status reads "down" until bin/wifi (step 11) connects
    display_devices_register(); // /dev/display/status reads "down" until bin/display (step 12) starts it
    thingspeak_devices_register(); // /dev/thingspeak/status reads "idle" until the first upload attempt (step 14)

    cat_register();
    echo_register();
    ls_register();
    lora_register();    // bin/lora -- see shell.cpp's boot script, which runs "lora &"
    wifi_register();    // bin/wifi -- see shell.cpp's boot script, which runs "wifi &"
    display_register(); // bin/display -- see shell.cpp's boot script, which runs "display &"

    jobs_init();
    task_register("shell",      task_shell,      30);    // 30ms: responsive to typing
    task_register("poll_room",  task_poll_room,  10000);  // see room.h -- 10s for now
    task_register("poll_lora",  task_poll_lora,  50);     // no-ops until the radio's started -- see lora.cpp
    task_register("display",    task_display,    50);     // no-ops until the OLED's started -- see display.cpp; 50ms so button presses feel instant
    task_register("thingspeak", task_thingspeak, 1000);   // no-ops until wifi's up -- see thingspeak.cpp; 1s is plenty, nothing here is user-facing
    task_register("console",    task_console,    100);    // drains klog (config_write warnings, lora auto-repair)

    while (1) {
        task_run();
        sleep_ms(1);
    }
}
