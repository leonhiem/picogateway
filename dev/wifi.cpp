/**
 * dev/wifi.cpp — /dev/wifi/status: down / connecting / up
 *
 * Read-only, straight from wifi.cpp's wifi_get_state(). `jobs` only
 * shows bin/wifi is scheduled, not whether it's actually associated --
 * this answers that, the way `nmcli`/`iwconfig` would on a real Linux
 * box: down (not connected, or waiting to retry), connecting
 * (association/DHCP in progress), up (connected, has an IP).
 */
#include "wifi.h"
#include "kernel/fs.h"
#include <cstdio>

static int status_read(char *buf, int len)
{
    switch (wifi_get_state()) {
        case WIFI_UP:         return snprintf(buf, len, "up\n");
        case WIFI_CONNECTING: return snprintf(buf, len, "connecting\n");
        default:              return snprintf(buf, len, "down\n");
    }
}

static const device_t dev_wifi_status = {"/dev/wifi/status", 0, 0, status_read, 0};

void wifi_devices_register(void)
{
    fs_register(&dev_wifi_status);
}
