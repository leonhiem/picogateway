/**
 * dev/thingspeak.cpp — /dev/thingspeak/status: idle / sending / ok / error
 *
 * Read-only, straight from thingspeak.cpp's thingspeak_get_status() --
 * same shape as dev/wifi.cpp/dev/lora.cpp's status devices. Lets you
 * `cat /dev/thingspeak/status` to see whether the last upload (room or
 * LoRa, whichever ran most recently) actually made it out, without
 * digging through the klog console output.
 */
#include "thingspeak.h"
#include "kernel/fs.h"
#include <cstdio>

static int status_read(char *buf, int len)
{
    switch (thingspeak_get_status()) {
        case THINGSPEAK_SENDING: return snprintf(buf, len, "sending\n");
        case THINGSPEAK_OK:      return snprintf(buf, len, "ok\n");
        case THINGSPEAK_ERROR:   return snprintf(buf, len, "error\n");
        default:                 return snprintf(buf, len, "idle\n");
    }
}

static const device_t dev_thingspeak_status = {"/dev/thingspeak/status", 0, 0, status_read, 0};

void thingspeak_devices_register(void)
{
    fs_register(&dev_thingspeak_status);
}
