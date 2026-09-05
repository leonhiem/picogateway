/**
 * wifi.h — the cyw43/wifi connection state, carved out of gateway.cpp's
 * old blocking connect-and-never-recheck code
 *
 * Same split as lora.h: this header (backed by wifi.cpp) owns the
 * hardware and the connection state machine, dev/wifi.cpp turns that
 * state into /dev/wifi/status, and prog/wifi.cpp (bin/wifi) is the
 * only thing that actually drives it, backgrounded from the boot
 * script the same way bin/lora is.
 *
 * Unlike LoRa (this board is the one radio "master" listening for
 * probes whenever they happen to transmit), wifi here is a client:
 * association with an access point is a multi-second round trip, not
 * a quick local SPI register check, and it can fail for reasons LoRa's
 * begin() never has to consider (wrong password, AP out of range,
 * AP simply not there). So the state has three values, not two:
 *
 *   down       -- not connected, no attempt in progress right now
 *                 (or the last attempt failed/timed out and we're
 *                 waiting out the retry backoff before trying again)
 *   connecting -- an attempt is in flight (association and/or DHCP
 *                 not finished yet)
 *   up         -- connected, with an IP address
 *
 * A link that was up and drops (AP goes away, etc.) goes straight back
 * to down, same as bin/lora's radio_ready does on a bad packet -- the
 * next background job tick notices and starts retrying on its own.
 */
#pragma once

#include "pico/stdlib.h"

typedef enum { WIFI_DOWN, WIFI_CONNECTING, WIFI_UP } wifi_state_t;

// One-time hardware bring-up (cyw43_arch_init() + enable_sta_mode()).
// Non-blocking and idempotent -- cyw43_arch_init() itself is a bounded
// call (chip reset + firmware load from flash), not an open-ended
// retry, so unlike the old gateway.cpp there's no need to loop here;
// callers just get told whether it's ready yet. Returns true once done
// (immediately, on every call after the first success).
bool wifi_arch_ready(void);

// Non-blocking: advances the connection state machine by one step,
// using the given credentials. Rate-limited the same way lora.cpp's
// lora_try_start() is (a real (re)connect attempt at most once every
// few seconds), so being re-invoked every job tick costs nothing extra
// while waiting on one. ssid/pass come from /dev/config/wifi_ssid and
// /dev/config/wifi_pass (the real, field-changeable EEPROM config from
// step 6) -- prog/wifi.cpp reads those devices and is the only caller
// of this function, same shape as prog/follow.cpp reading its gate and
// source devices before acting.
void wifi_try_connect(const char *ssid, const char *pass);

// Current state -- what /dev/wifi/status (dev/wifi.cpp) reports.
wifi_state_t wifi_get_state(void);

// dev/wifi.cpp -- registers /dev/wifi/status. Call once from main().
void wifi_devices_register(void);
