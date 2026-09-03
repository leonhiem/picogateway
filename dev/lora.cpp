/**
 * dev/lora.cpp — /dev/lora/probe<1..3>/{temp,soil,ph,rssi,age,seen}
 *
 * Read-only, formats whatever lora.cpp's task_poll_lora last cached --
 * same read-a-cache shape as /dev/buttons/<name> and /dev/room/*.
 *
 * temp/soil/ph/rssi/age are always plain numbers (0.0 / 0 / -1 before
 * a probe's first packet ever arrives) -- anything reading them with
 * strtof()/atoi() shouldn't have to special-case a "never" string.
 * /dev/lora/probe<n>/seen is the device to check first if you need to
 * tell "genuinely 0.0" apart from "no packet yet" -- "0\n"/"1\n", same
 * convention as /dev/buttons/<name>. age reads -1 in that same
 * "nothing yet" case, since 0 there would misleadingly look like "a
 * packet just arrived".
 */
#include "lora.h"
#include "kernel/fs.h"
#include "pico/time.h"
#include <cstdio>

static int temp_read(int idx, char *buf, int len)
{
    return snprintf(buf, len, "%.1f\n", probe_temp[idx]);
}

static int soil_read(int idx, char *buf, int len)
{
    return snprintf(buf, len, "%.1f\n", probe_soil[idx]);
}

static int ph_read(int idx, char *buf, int len)
{
    return snprintf(buf, len, "%.1f\n", probe_ph[idx]);
}

static int rssi_read(int idx, char *buf, int len)
{
    return snprintf(buf, len, "%d\n", probe_rssi[idx]);
}

static int age_read(int idx, char *buf, int len)
{
    if (!probe_seen[idx]) return snprintf(buf, len, "-1\n");
    uint32_t age_ms = to_ms_since_boot(get_absolute_time()) - probe_last_seen_ms[idx];
    return snprintf(buf, len, "%lu\n", (unsigned long)(age_ms / 1000));
}

static int seen_read(int idx, char *buf, int len)
{
    return snprintf(buf, len, probe_seen[idx] ? "1\n" : "0\n");
}

// device_t's read() takes no context param, so one small wrapper per
// probe x field -- same shape as dev/buttons.cpp's up_read/down_read/...
// and dev/config.cpp's CONFIG_DEVICE macro.
#define LORA_DEVICE(IDX)                                                    \
    static int probe##IDX##_temp_read(char *buf, int len) { return temp_read(IDX, buf, len); } \
    static int probe##IDX##_soil_read(char *buf, int len) { return soil_read(IDX, buf, len); } \
    static int probe##IDX##_ph_read(char *buf, int len)   { return ph_read(IDX, buf, len); }    \
    static int probe##IDX##_rssi_read(char *buf, int len) { return rssi_read(IDX, buf, len); } \
    static int probe##IDX##_age_read(char *buf, int len)  { return age_read(IDX, buf, len); }   \
    static int probe##IDX##_seen_read(char *buf, int len) { return seen_read(IDX, buf, len); }

LORA_DEVICE(0)
LORA_DEVICE(1)
LORA_DEVICE(2)

static const device_t devs[LORA_PROBE_COUNT * 6] = {
    {"/dev/lora/probe1/temp", 0, 0, probe0_temp_read, 0},
    {"/dev/lora/probe1/soil", 0, 0, probe0_soil_read, 0},
    {"/dev/lora/probe1/ph",   0, 0, probe0_ph_read,   0},
    {"/dev/lora/probe1/rssi", 0, 0, probe0_rssi_read, 0},
    {"/dev/lora/probe1/age",  0, 0, probe0_age_read,  0},
    {"/dev/lora/probe1/seen", 0, 0, probe0_seen_read, 0},

    {"/dev/lora/probe2/temp", 0, 0, probe1_temp_read, 0},
    {"/dev/lora/probe2/soil", 0, 0, probe1_soil_read, 0},
    {"/dev/lora/probe2/ph",   0, 0, probe1_ph_read,   0},
    {"/dev/lora/probe2/rssi", 0, 0, probe1_rssi_read, 0},
    {"/dev/lora/probe2/age",  0, 0, probe1_age_read,  0},
    {"/dev/lora/probe2/seen", 0, 0, probe1_seen_read, 0},

    {"/dev/lora/probe3/temp", 0, 0, probe2_temp_read, 0},
    {"/dev/lora/probe3/soil", 0, 0, probe2_soil_read, 0},
    {"/dev/lora/probe3/ph",   0, 0, probe2_ph_read,   0},
    {"/dev/lora/probe3/rssi", 0, 0, probe2_rssi_read, 0},
    {"/dev/lora/probe3/age",  0, 0, probe2_age_read,  0},
    {"/dev/lora/probe3/seen", 0, 0, probe2_seen_read, 0},
};

void lora_devices_register(void)
{
    for (unsigned i = 0; i < LORA_PROBE_COUNT * 6; i++) fs_register(&devs[i]);
}
