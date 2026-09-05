/**
 * lora.cpp — LoRa radio init + packet poll/parse, carved out of
 * gateway.cpp's main loop
 *
 * task_poll_lora() is gateway.cpp's own LoRa.parsePacket()/available()
 * loop and its "=<id>&field1=..&field2=..&field3=.." parser, with two
 * differences:
 *
 *  - field extraction uses strstr() on a properly null-terminated,
 *    correctly-sized local buffer instead of gateway.cpp's fixed byte
 *    offsets into its shared, reused-for-everything-else `buf` -- same
 *    wire format, just not relying on exact spacing ("&field1=" being
 *    exactly 8 bytes, etc.) or on stale bytes left over from whatever
 *    `buf` was last used for.
 *
 *  - an incomplete or unparseable packet is dropped without caching
 *    anything (same "ignored junk packet" contract gateway.cpp had),
 *    rather than partially overwriting a probe's last-known-good
 *    reading with whatever did parse.
 *
 * The auto-repair gateway.cpp already had (an implausibly long packet
 * -> assume the radio wedged -> LoRa.begin() again) is kept as-is;
 * that's still the only "something's wrong" signal available. A
 * silence timeout isn't a safe substitute -- with 3 independent probes
 * transmitting at their own random pace, "haven't heard from probe 2
 * in an hour" is completely normal, not a fault.
 */
#include "lora.h"
#include "kernel/klog.h"
#include "LoRa-RP2040.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#define LORA_MAX_PACKET 100 // matches gateway.cpp's own bad-packet-length threshold
#define LORA_RX_BUF (LORA_MAX_PACKET + 1)

volatile bool     probe_seen[LORA_PROBE_COUNT];
volatile float    probe_temp[LORA_PROBE_COUNT];
volatile float    probe_soil[LORA_PROBE_COUNT];
volatile float    probe_ph[LORA_PROBE_COUNT];
volatile int      probe_rssi[LORA_PROBE_COUNT];
volatile uint32_t probe_last_seen_ms[LORA_PROBE_COUNT];

// False until the radio has actually come up -- lets task_poll_lora()
// be registered unconditionally at boot (gateway.cpp step 9) while
// radio bring-up itself stays deferred to bin/lora (step 10), without
// task_poll_lora() touching an uninitialized SPI bus in the meantime.
static volatile bool radio_ready = false;

// Rate limit for lora_try_start()'s real attempts -- see there.
#define LORA_RETRY_MS 3000
static uint32_t last_attempt_ms = 0;

static bool try_once(void)
{
    if (LoRa.begin(868E6)) {
        radio_ready = true;
    }
    return radio_ready;
}

// Old blocking entry point -- kept only for demo_lora.cpp, which calls
// this once at boot before its task loop starts (nothing else is
// running yet, so blocking there costs nothing). The real gateway
// firmware never calls this -- see lora_try_start() below.
void lora_init(void)
{
    while (!try_once()) {
        sleep_ms(3000);
    }
}

// Non-blocking: attempts LoRa.begin() at most once every LORA_RETRY_MS,
// otherwise just reports current status without touching the radio.
// Safe to call every tick from a background job (prog/lora.cpp) --
// once up, this is just an idle bool check; while down, extra calls
// between retries are free no-ops instead of extra attempts.
bool lora_try_start(void)
{
    if (radio_ready) return true;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (last_attempt_ms != 0 && now - last_attempt_ms < LORA_RETRY_MS) return false;
    last_attempt_ms = now;
    return try_once();
}

bool lora_is_ready(void)
{
    return radio_ready;
}

static const char *find_field(const char *hay, const char *key)
{
    const char *p = strstr(hay, key);
    return p ? p + strlen(key) : NULL;
}

static bool parse_value(const char *start, float *out)
{
    char tmp[16];
    int n = 0;
    while (start[n] && start[n] != '&' && n < (int)sizeof(tmp) - 1) {
        tmp[n] = start[n];
        n++;
    }
    tmp[n] = '\0';
    if (n == 0) return false;

    char *end;
    float v = strtof(tmp, &end);
    if (end == tmp) return false; // not a number at all
    *out = v;
    return true;
}

static void handle_packet(const char *buf)
{
    if (buf[0] != '=' || buf[1] < '1' || buf[1] > '3') {
        return; // junk or out-of-range id -- same contract as gateway.cpp
    }
    int idx = buf[1] - '1'; // probe (idx+1)

    const char *f1 = find_field(buf, "field1=");
    const char *f2 = find_field(buf, "field2=");
    const char *f3 = find_field(buf, "field3=");
    if (!f1 || !f2 || !f3) return; // incomplete packet

    float temp, soil, ph;
    if (!parse_value(f1, &temp) || !parse_value(f2, &soil) || !parse_value(f3, &ph)) return;

    probe_temp[idx] = temp;
    probe_soil[idx] = soil;
    probe_ph[idx]   = ph;
    probe_rssi[idx] = LoRa.packetRssi();
    probe_last_seen_ms[idx] = to_ms_since_boot(get_absolute_time());
    probe_seen[idx] = true;
}

void task_poll_lora(void)
{
    if (!radio_ready) return; // lora_init() hasn't run yet -- see bin/lora, step 10

    int packetSize = LoRa.parsePacket();
    if (!packetSize) return;

    if (packetSize > LORA_MAX_PACKET) {
        klog("lora", "packet len=%d implausible, marking radio down for restart", packetSize);
        radio_ready = false; // prog/lora.cpp's background job notices next
                              // tick and calls lora_try_start() again --
                              // no blocking retry loop here any more (that
                              // used to freeze the whole cooperative
                              // scheduler until the radio came back)
        return;
    }

    char buf[LORA_RX_BUF];
    int n = 0;
    while (LoRa.available()) {
        int c = LoRa.read();
        if (n < LORA_MAX_PACKET) buf[n++] = (char)c;
    }
    buf[n] = '\0';

    handle_packet(buf);
}
