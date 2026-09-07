/**
 * thingspeak.h — periodic ThingSpeak HTTP uploads, carved out of
 * gateway.cpp's old run_tcp_client()/TCP_CLIENT_T
 *
 * Same "read the real value through kernel/fs.h, never touch another
 * module's cache directly" idiom display.cpp and prog/follow.cpp use --
 * this module is a *consumer* of /dev/room/*, /dev/lora/probe<n>/*,
 * /dev/config/{url,api0..3} and /dev/wifi/status, not the owner of any
 * of them.
 *
 * Unlike lora.cpp/wifi.cpp/display.cpp, there's no hardware bring-up to
 * defer here (no chip, no bus, no radio to bring up) -- the only
 * precondition is "wifi is up", which is just another device read away
 * -- so there's no bin/thingspeak/boot-script entry. task_thingspeak()
 * is registered unconditionally in gateway.cpp, same as task_poll_room:
 * nothing to start, just something to gate on every tick.
 *
 * Two independent uploads share one HTTP client (only one request is
 * ever in flight at a time -- ThingSpeak's own per-channel rate limit
 * makes overlapping uploads pointless anyway):
 *
 *   - the local room sensors (temp/hum/light), every 60s, to the
 *     channel configured at /dev/config/api0
 *   - one LoRa probe's cached values (temp/soil/ph), round-robin over
 *     whichever of probe 1/2/3 have ever actually reported, every 60s
 *     but offset 30s from the room upload -- "somewhere in between",
 *     per the user's own request. Probes only transmit roughly every
 *     15 minutes anyway, so re-checking each one this often is cheap
 *     and just means a probe's latest reading reaches ThingSpeak
 *     promptly whichever tick it lands on, not a request to hammer it.
 *
 * The old run_tcp_client() blocked for seconds at a time (DNS lookup in
 * a sleep_ms(5000) spin loop, a flat sleep_ms(5000) after the write,
 * watchdog_update() sprinkled through both to survive it) -- none of
 * that exists here. DNS/connect/send/close all run through lwIP's own
 * async tcp_* / dns_gethostbyname() callbacks (same primitives the
 * original used, just not blocked on), so a stuck or slow upload can
 * never stall the scheduler; a per-connection tcp_poll() backstop fails
 * it out after a few seconds of silence either way.
 */
#pragma once

#include "pico/stdlib.h"

typedef enum {
    THINGSPEAK_IDLE,    // no upload has been attempted yet this boot
    THINGSPEAK_SENDING,  // DNS/connect/send in progress right now
    THINGSPEAK_OK,       // the most recently completed upload succeeded
    THINGSPEAK_ERROR,    // the most recently completed upload failed
} thingspeak_status_t;

// The scheduler + HTTP client. No-ops whenever /dev/wifi/status isn't
// "up", or while a previous upload is still in flight -- safe to
// register at any period; see thingspeak.cpp for the actual 60s/30s
// timing. Run less often than task_display -- there's nothing here an
// impatient user is waiting on.
void task_thingspeak(void);

// What /dev/thingspeak/status (dev/thingspeak.cpp) reports.
thingspeak_status_t thingspeak_get_status(void);

// dev/thingspeak.cpp -- registers /dev/thingspeak/status. Call once
// from main().
void thingspeak_devices_register(void);
