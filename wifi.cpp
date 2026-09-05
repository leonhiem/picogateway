/**
 * wifi.cpp — wifi connection state machine, carved out of gateway.cpp's
 * old cyw43_arch_init()/cyw43_arch_wifi_connect_timeout_ms() startup
 * code
 *
 * The old code blocked for up to 30s in cyw43_arch_wifi_connect_timeout_ms()
 * and never rechecked the link afterwards -- if it ever dropped, the
 * only recovery was gateway.cpp's watchdog rebooting the whole board.
 * Here, wifi_try_connect() only ever takes one non-blocking step:
 *
 *   not initialized -- bring up the cyw43 chip + station mode
 *   down            -- kick off cyw43_arch_wifi_connect_async() (rate
 *                       limited -- see WIFI_RETRY_MS) and move to connecting
 *   connecting      -- check cyw43_tcpip_link_status(): UP -> up,
 *                       still working (JOIN/NOIP) -> stay connecting
 *                       (but see WIFI_CONNECT_TIMEOUT_MS below), anything
 *                       else (FAIL/NONET/BADAUTH) -> back to down,
 *                       retried after the backoff
 *   up              -- check the link is still UP; if it ever isn't,
 *                       back to down, same as any other failure
 *
 * cyw43_arch_wifi_connect_async()/cyw43_tcpip_link_status() themselves
 * are quick register/state checks, not blocking waits -- the actual
 * association happens in the background via the cyw43 driver's own
 * IRQ-driven poll (pico_cyw43_arch_lwip_threadsafe_background), which
 * is why this can be safely called every bin/wifi job tick without
 * ever stalling the scheduler.
 *
 * IMPORTANT: this polls cyw43_tcpip_link_status(), not the more
 * obviously-named cyw43_wifi_link_status() -- an actual bug caught by
 * hardware testing (status stuck at "connecting" forever, on every
 * single attempt, reproduced even with nothing else in the whole
 * architecture running). Turned out cyw43_wifi_link_status() (checked
 * in cyw43-driver/src/cyw43_ctrl.c) can *only ever* return
 * DOWN/JOIN/FAIL/NONET/BADAUTH -- there is no code path in it that
 * returns UP or NOIP at all, because it only reflects the raw
 * wifi-join state machine, not whether lwIP's netif is actually up
 * with an IP address. Polling it alone, this code could never observe
 * success no matter how long it waited, on a perfectly good
 * connection. cyw43_tcpip_link_status() is the one that additionally
 * checks the netif flags/IP before falling back to the wifi-only
 * check -- it's what the SDK's own cyw43_arch_wifi_connect_until()
 * polls, and the reason the *original* gateway.cpp (via
 * cyw43_arch_wifi_connect_timeout_ms(), a thin wrapper around exactly
 * that) always worked. WIFI_CONNECT_TIMEOUT_MS below is kept anyway as
 * a defensive backstop, not a workaround for this -- a genuinely wedged
 * association is still possible in principle and this makes it
 * self-heal either way.
 *
 * Every state transition (and why) goes through klog -- the old code
 * printed straight to the console on a connect failure; klog_flush()
 * (gateway.cpp's task_console) is this architecture's equivalent, and
 * unlike a raw printf it's safe to call from here regardless of which
 * task happens to be running.
 *
 * The connect attempt log line prints the password in plain text, on
 * request, specifically so it can be eyeballed against the router's
 * actual configured password over the serial console -- this is a
 * local debug console, not a shared/remote log, so that's the same
 * exposure as running `cat /dev/config/wifi_pass` directly.
 */
#include "wifi.h"
#include "kernel/klog.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"

// Both match gateway.cpp's old values exactly (cyw43_arch_wifi_connect_timeout_ms(...,
// 30000) for the single-attempt timeout, sleep_ms_cond(30000) for the
// backoff between attempts) -- not just "don't hammer it" placeholders.
// A short backoff especially matters: many routers temporarily
// throttle/blacklist a MAC address after repeated failed association
// attempts in a short window, which looks from here exactly like a
// permanently stuck join, attempt after attempt, with no clean
// failure ever reported -- retrying every few seconds is likely to
// keep re-triggering that instead of giving it time to clear.
#define WIFI_RETRY_MS           30000
#define WIFI_CONNECT_TIMEOUT_MS 30000

static bool arch_ready = false;
static wifi_state_t state = WIFI_DOWN;
static uint32_t last_attempt_ms = 0;
static uint32_t connecting_since_ms = 0;

static const char *link_status_str(int link)
{
    switch (link) {
        case CYW43_LINK_DOWN:    return "down";
        case CYW43_LINK_JOIN:    return "join";
        case CYW43_LINK_NOIP:    return "noip";
        case CYW43_LINK_UP:      return "up";
        case CYW43_LINK_FAIL:    return "fail";
        case CYW43_LINK_NONET:   return "nonet (no matching SSID -- check /dev/config/wifi_ssid)";
        case CYW43_LINK_BADAUTH: return "badauth (wrong password? check /dev/config/wifi_pass)";
        default:                 return "?";
    }
}

bool wifi_arch_ready(void)
{
    if (arch_ready) return true;
    if (cyw43_arch_init()) {
        klog("wifi", "cyw43_arch_init() failed, retrying");
        return false; // chip/firmware init failed -- try again next call
    }
    cyw43_arch_enable_sta_mode();
    arch_ready = true;
    klog("wifi", "cyw43 station mode ready");
    return true;
}

wifi_state_t wifi_get_state(void)
{
    return state;
}

void wifi_try_connect(const char *ssid, const char *pass)
{
    if (!wifi_arch_ready()) return;

    if (state == WIFI_UP || state == WIFI_CONNECTING) {
        int link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (link == CYW43_LINK_UP) {
            if (state != WIFI_UP) klog("wifi", "connected");
            state = WIFI_UP;
            return;
        }
        if (link == CYW43_LINK_JOIN || link == CYW43_LINK_NOIP) {
            uint32_t now = to_ms_since_boot(get_absolute_time());
            if (state == WIFI_CONNECTING && now - connecting_since_ms > WIFI_CONNECT_TIMEOUT_MS) {
                klog("wifi", "still '%s' after %lus, giving up for now",
                     link_status_str(link), (unsigned long)(WIFI_CONNECT_TIMEOUT_MS / 1000));
                state = WIFI_DOWN; // fall through below, retried after WIFI_RETRY_MS
            } else {
                state = WIFI_CONNECTING; // association/DHCP still in progress
                return;
            }
        } else {
            // CYW43_LINK_DOWN/FAIL/NONET/BADAUTH -- attempt is over, one
            // way or another.
            if (state == WIFI_UP) klog("wifi", "link dropped (%s)", link_status_str(link));
            else                  klog("wifi", "connect failed: %s", link_status_str(link));
            state = WIFI_DOWN; // fall through below, retried after the backoff
        }
    }

    // state == WIFI_DOWN here
    if (ssid[0] == '\0') return; // /dev/config/wifi_ssid not set yet -- nothing to try

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (last_attempt_ms != 0 && now - last_attempt_ms < WIFI_RETRY_MS) return;
    last_attempt_ms = now;

    klog("wifi", "connecting to '%s' / '%s'", ssid, pass);
    if (cyw43_arch_wifi_connect_async(ssid, pass, CYW43_AUTH_WPA2_AES_PSK) == 0) {
        state = WIFI_CONNECTING;
        connecting_since_ms = now;
    } else {
        klog("wifi", "cyw43_arch_wifi_connect_async() couldn't even start");
        // stays WIFI_DOWN, retried after the next backoff window
    }
}
