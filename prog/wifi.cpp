/**
 * prog/wifi.cpp — wifi: bring up (or re-bring-up) the wifi connection
 *
 * Step 11: gateway.cpp's main() never calls into wifi.cpp directly --
 * wifi_try_connect() only runs from here, through the same
 * device-reading shape prog/follow.cpp uses for its gate/source
 * devices: open /dev/config/wifi_ssid and /dev/config/wifi_pass (the
 * real EEPROM config from step 6, field-changeable without reflashing,
 * not a compiled-in WIFI_SSID/WIFI_PASSWORD), strip the trailing
 * newline config_read() always adds, and pass the result to
 * wifi_try_connect() -- one non-blocking step per call, same contract
 * as prog/lora.cpp's lora_try_start().
 *
 * Meant to run backgrounded from the boot script:
 *
 *   wifi &
 *
 * wifi_try_connect() is itself rate-limited, so being re-invoked every
 * JOB_POLL_MS costs nothing once up (an idle status check) or while
 * down (skipped attempts, not extra ones). If the link ever drops,
 * wifi.cpp notices on its own and goes back to "down" -- this job's
 * next tick picks that up and starts retrying automatically. Also
 * runnable in the foreground (`wifi`) to force an immediate status
 * check / retry attempt right now.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include "wifi.h"
#include <cstdio>
#include <cctype>

// Reads one /dev/config/* field into buf, trimming trailing whitespace
// (not just the '\n' config_read() (dev/config.cpp) always adds, but
// also any space/tab/CR that ended up stored in the EEPROM itself --
// e.g. a value ever entered through gateway.cpp's old fixed-width OLED
// menu editor, which right-padded fields with spaces up to its own
// column width. A trailing space in a wifi password or SSID makes it
// not match the AP's actual one at all, silently -- cyw43 has no way
// to tell "wrong because of an invisible extra character" apart from
// any other wrong password. Leaves buf empty (not touched) on any
// failure to open/read -- wifi_try_connect() treats an empty ssid as
// "nothing configured yet" and simply does nothing.
static void read_field(const char *path, char *buf, int buflen)
{
    buf[0] = '\0';
    int fd = fs_open(path);
    if (fd < 0) return;
    int n = fs_read(fd, buf, buflen - 1);
    fs_close(fd);
    if (n < 0) return;
    while (n > 0 && isspace((unsigned char)buf[n - 1])) n--;
    buf[n] = '\0';
}

static int wifi_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    (void)argc;
    (void)argv;

    char ssid[33];
    char pass[33];
    read_field("/dev/config/wifi_ssid", ssid, sizeof(ssid));
    read_field("/dev/config/wifi_pass", pass, sizeof(pass));

    wifi_try_connect(ssid, pass);

    switch (wifi_get_state()) {
        case WIFI_UP:         return snprintf(out, outlen, "up\n");
        case WIFI_CONNECTING: return snprintf(out, outlen, "connecting\n");
        default:              return snprintf(out, outlen, "down\n");
    }
}

static const program_t prog_wifi = {"wifi", wifi_run};

void wifi_register(void)
{
    prog_register(&prog_wifi);
}
