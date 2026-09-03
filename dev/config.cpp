/**
 * dev/config.cpp — /dev/config/{wifi_ssid,wifi_pass,url,farm,api0..3}
 *
 * Read/write, backed by the EEPROM (eeprom.h). This is the part
 * gateway.cpp's own review turned up as thin: it read raw EEPROM bytes
 * straight into wifi_ssid/wifi_passwd/etc. and used them as-is, with no
 * check that what came back was even a valid string -- a corrupted or
 * blank (0xFF-filled) chip would feed garbage directly into the wifi
 * connect call. Two checks close that gap, one on each side:
 *
 *  - write: sanitized into a fixed EEPROM_SLOT_SIZE-byte slot, always
 *    zero-padded past the content, so a short value overwriting a
 *    longer one can't leave old bytes stranded past its terminator
 *    (gateway.cpp's variable-length strlen()+1 writes could -- and its
 *    factory-default writes, using strlen() with no +1 at all, would
 *    have left an *unterminated* string if a prior EEPROM_CLR pass
 *    hadn't already zeroed the tail first). eeprom_write_verified()
 *    (eeprom.cpp) then confirms the chip actually holds it.
 *
 *  - read: the slot is only trusted if it contains a null terminator
 *    and everything before it is printable ASCII; anything else is
 *    reported as an empty field (and logged) rather than passed
 *    through raw.
 */
#include "eeprom.h"
#include "kernel/fs.h"
#include "kernel/klog.h"
#include <cstdio>

// One slot byte is always reserved for the guaranteed terminator (see
// config_write below), so the longest string a field can actually
// hold is one less than the slot -- still longer than the OLED menu's
// own 21-char edit range ever allowed.
#define CONFIG_STR_MAX (EEPROM_SLOT_SIZE - 1)

typedef struct {
    const char *devname;
    uint8_t     page;
    uint8_t     addr;
} config_field_t;

static const config_field_t fields[] = {
    {"/dev/config/wifi_ssid", 0, EEPROM_OFFS_WifiSSID},
    {"/dev/config/wifi_pass", 0, EEPROM_OFFS_WifiPass},
    {"/dev/config/url",       0, EEPROM_OFFS_URL},
    {"/dev/config/farm",      0, EEPROM_OFFS_FARM},
    {"/dev/config/api0",      1, EEPROM_OFFS_API0},
    {"/dev/config/api1",      1, EEPROM_OFFS_API1},
    {"/dev/config/api2",      1, EEPROM_OFFS_API2},
    {"/dev/config/api3",      1, EEPROM_OFFS_API3},
};
#define CONFIG_FIELD_COUNT (sizeof(fields) / sizeof(fields[0]))

static int config_read(int idx, char *buf, int len)
{
    uint8_t raw[EEPROM_SLOT_SIZE];
    eeprom_read_raw(fields[idx].page, fields[idx].addr, raw);

    int term = -1;
    for (int i = 0; i < EEPROM_SLOT_SIZE; i++) {
        if (raw[i] == '\0') { term = i; break; }
    }
    bool valid = (term >= 0);
    for (int i = 0; valid && i < term; i++) {
        if (raw[i] < 0x20 || raw[i] > 0x7E) valid = false;
    }

    if (!valid) {
        klog("config", "%s: corrupt/unterminated slot, reporting empty", fields[idx].devname);
        return snprintf(buf, len, "\n");
    }
    return snprintf(buf, len, "%s\n", (const char *)raw);
}

static int config_write(int idx, const char *buf, int len)
{
    uint8_t slot[EEPROM_SLOT_SIZE] = {0}; // zero-init: guarantees the
                                           // tail (and so the
                                           // terminator) regardless of
                                           // how little gets copied in
    int n = 0;
    for (int i = 0; i < len && n < CONFIG_STR_MAX; i++) {
        char c = buf[i];
        if (c == '\n' || c == '\r') break; // one line in, like echo's argv join
        if (c < 0x20 || c > 0x7E) continue; // drop non-printable rather than store it
        slot[n++] = (uint8_t)c;
    }

    if (!eeprom_write_verified(fields[idx].page, fields[idx].addr, slot)) {
        klog("config", "%s: write failed verification", fields[idx].devname);
        return -1;
    }
    return len;
}

// device_t's read()/write() take no context param, so one small
// wrapper per field -- same shape as dev/buttons.cpp's up_read/
// down_read/... around its shared one_read(idx, ...).
#define CONFIG_DEVICE(FIELD_IDX, SUFFIX)                                    \
    static int SUFFIX##_read(char *buf, int len) {                         \
        return config_read(FIELD_IDX, buf, len);                           \
    }                                                                       \
    static int SUFFIX##_write(const char *buf, int len) {                  \
        return config_write(FIELD_IDX, buf, len);                          \
    }

CONFIG_DEVICE(0, wifi_ssid)
CONFIG_DEVICE(1, wifi_pass)
CONFIG_DEVICE(2, url)
CONFIG_DEVICE(3, farm)
CONFIG_DEVICE(4, api0)
CONFIG_DEVICE(5, api1)
CONFIG_DEVICE(6, api2)
CONFIG_DEVICE(7, api3)

static const device_t devs[CONFIG_FIELD_COUNT] = {
    {"/dev/config/wifi_ssid", 0, 0, wifi_ssid_read, wifi_ssid_write},
    {"/dev/config/wifi_pass", 0, 0, wifi_pass_read, wifi_pass_write},
    {"/dev/config/url",       0, 0, url_read,       url_write},
    {"/dev/config/farm",      0, 0, farm_read,      farm_write},
    {"/dev/config/api0",      0, 0, api0_read,      api0_write},
    {"/dev/config/api1",      0, 0, api1_read,      api1_write},
    {"/dev/config/api2",      0, 0, api2_read,      api2_write},
    {"/dev/config/api3",      0, 0, api3_read,      api3_write},
};

void config_register(void)
{
    for (unsigned i = 0; i < CONFIG_FIELD_COUNT; i++) fs_register(&devs[i]);
}
