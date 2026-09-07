/**
 * thingspeak.cpp — the async HTTP client + the 60s/30s upload scheduler
 *
 * See thingspeak.h's header comment for the overall shape. Two things
 * live in this file:
 *
 *  - a tiny non-blocking HTTP/1.1 GET client (start_request() and the
 *    tcp_*_cb callbacks below) built directly on lwIP's tcp_pcb API and
 *    dns_gethostbyname(), same primitives gateway.cpp's old
 *    run_tcp_client() used -- but driven entirely by lwIP's own
 *    callbacks instead of a blocking sleep_ms() loop. Only one request
 *    is ever in flight (client_state != CLIENT_IDLE guards start_
 *    request()), so unlike the original's heap-allocated per-request
 *    TCP_CLIENT_T, this is just a handful of module-static variables --
 *    no calloc()/free() anywhere.
 *
 *  - task_thingspeak(), the scheduler that decides *when* to call
 *    start_request() and with what -- room sensors every 60s, one LoRa
 *    probe (round-robin, skipping any never actually seen) every 60s
 *    offset 30s from the room upload.
 *
 * "Success" here means the request was fully sent and acknowledged by
 * the TCP stack (tcp_sent_cb reports every byte acked) -- same level of
 * rigor the original had anyway: it read a response into a buffer via
 * tcp_client_recv() but never actually parsed it, just waited a flat
 * 5s and moved on. We don't parse the response either, we just don't
 * need to fake-wait for it -- except the one line worth seeing while
 * diagnosing a bad key or malformed request: the first line of
 * whatever comes back (tcp_recv_cb), logged once per request.
 *
 * Every stage klogs (start, DNS resolve, connect, response, success/
 * failure), each line tagged with which upload it belongs to ("room"/
 * "probe1"/"probe2"/"probe3", active_label) -- there are two independent
 * upload streams sharing one client, so without the tag a console
 * watching `cat`/klog_flush output couldn't tell which was which.
 */
#include "thingspeak.h"
#include "kernel/fs.h"
#include "kernel/klog.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#define TCP_PORT 80

// ─── scheduling ────────────────────────────────────────────────────────
#define ROOM_UPLOAD_INTERVAL_MS 60000
#define LORA_UPLOAD_INTERVAL_MS 60000
#define LORA_UPLOAD_OFFSET_MS   30000 // "somewhere in between" the room uploads
#define LORA_PROBE_COUNT        3     // matches lora.h's LORA_PROBE_COUNT; not
                                      // included here for the same reason
                                      // display.cpp doesn't -- this is a
                                      // /dev/lora/* consumer, not lora.h's owner

static absolute_time_t next_room_upload;
static absolute_time_t next_lora_upload;
static bool timers_seeded = false;
static int  lora_probe_rr = 0; // 0..2 -> probe 1..3, round-robin position

// ─── small helpers (same shape as display.cpp's read_field) ────────────
static void read_field(const char *path, char *out, int outlen)
{
    out[0] = '\0';
    int fd = fs_open(path);
    if (fd < 0) return;
    int n = fs_read(fd, out, outlen - 1);
    fs_close(fd);
    if (n < 0) return;
    if (n > 0 && out[n - 1] == '\n') n--;
    out[n] = '\0';
}

static bool wifi_is_up(void)
{
    char buf[16];
    read_field("/dev/wifi/status", buf, sizeof(buf));
    return strcmp(buf, "up") == 0;
}

// ─── the HTTP client ─────────────────────────────────────────────────
typedef enum {
    CLIENT_IDLE,
    CLIENT_RESOLVING,
    CLIENT_CONNECTING,
    CLIENT_SENDING,
} client_state_t;

static client_state_t     client_state = CLIENT_IDLE;
static thingspeak_status_t status = THINGSPEAK_IDLE;

static struct tcp_pcb *pcb = NULL;

static char     cached_host[40] = "";
static ip_addr_t cached_addr;
static bool     have_ip = false;

static char request[192];
static int  request_len;
static int  sent_len;
static bool response_logged; // have we already klog'd this request's first response bytes?

static char active_label[16] = "?"; // "room"/"probe1"/... -- which upload is in flight, for klog

thingspeak_status_t thingspeak_get_status(void) { return status; }

static void close_pcb(void)
{
    if (!pcb) return;
    tcp_arg(pcb, NULL);
    tcp_poll(pcb, NULL, 0);
    tcp_sent(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
    pcb = NULL;
}

static void succeed_request(void)
{
    close_pcb();
    status = THINGSPEAK_OK;
    client_state = CLIENT_IDLE;
    klog("thingspeak", "%s: upload ok", active_label);
}

static void fail_request(const char *why)
{
    klog("thingspeak", "%s: upload failed: %s", active_label, why);
    have_ip = false; // force a fresh DNS lookup next time, in case the old IP's stale
    close_pcb();
    status = THINGSPEAK_ERROR;
    client_state = CLIENT_IDLE;
}

static err_t tcp_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    (void)arg; (void)tpcb;
    sent_len += len;
    if (sent_len >= request_len) succeed_request();
    return ERR_OK;
}

static err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    (void)arg; (void)err;
    if (!p) {
        // remote closed its end
        if (pcb) {
            if (sent_len >= request_len) succeed_request();
            else fail_request("connection closed before request fully sent");
        }
        return ERR_OK;
    }
    if (!response_logged) {
        // Just the first line (e.g. "HTTP/1.1 200 OK", or ThingSpeak's own
        // "HTTP/1.1 400 Bad Request" if a field/api_key was rejected) --
        // we don't otherwise parse the response at all, but this is the
        // one line worth seeing on the console while diagnosing a bad key
        // or malformed request.
        char preview[48];
        int n = p->len < (int)sizeof(preview) - 1 ? p->len : (int)sizeof(preview) - 1;
        memcpy(preview, p->payload, n);
        preview[n] = '\0';
        for (int i = 0; i < n; i++) {
            if (preview[i] == '\r' || preview[i] == '\n') { preview[i] = '\0'; break; }
        }
        klog("thingspeak", "%s: response: %s", active_label, preview);
        response_logged = true;
    }
    tcp_recved(tpcb, p->tot_len); // we don't parse the response any further, just keep the window open
    pbuf_free(p);
    return ERR_OK;
}

static err_t tcp_poll_cb(void *arg, struct tcp_pcb *tpcb)
{
    (void)arg; (void)tpcb;
    fail_request("timed out"); // same magnitude as the original's POLL_TIME_S*2 (~10s)
    return ERR_OK;
}

static void tcp_err_cb(void *arg, err_t err)
{
    (void)arg;
    if (err == ERR_ABRT) return; // we caused this ourselves via close_pcb()'s tcp_abort
    pcb = NULL; // lwIP already freed the pcb before this callback -- must not touch it
    char why[32];
    snprintf(why, sizeof(why), "connection error (%d)", (int)err);
    fail_request(why);
}

static err_t tcp_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK) {
        char why[32];
        snprintf(why, sizeof(why), "connect failed (%d)", (int)err);
        fail_request(why);
        return ERR_OK;
    }
    klog("thingspeak", "%s: connected, sending %d bytes", active_label, request_len);

    cyw43_arch_lwip_begin();
    err_t werr = tcp_write(tpcb, request, request_len, TCP_WRITE_FLAG_COPY);
    cyw43_arch_lwip_end();
    if (werr != ERR_OK) { fail_request("tcp_write failed"); return ERR_OK; }

    client_state = CLIENT_SENDING;
    return ERR_OK;
}

static void begin_connect(void)
{
    pcb = tcp_new_ip_type(IP_GET_TYPE(&cached_addr));
    if (!pcb) { fail_request("tcp_new failed"); return; }

    tcp_arg(pcb, NULL);
    tcp_poll(pcb, tcp_poll_cb, 10); // ~5s (10 * the ~500ms TCP slow-timer interval)
    tcp_sent(pcb, tcp_sent_cb);
    tcp_recv(pcb, tcp_recv_cb);
    tcp_err(pcb, tcp_err_cb);

    sent_len = 0;
    client_state = CLIENT_CONNECTING;
    klog("thingspeak", "%s: connecting to %s:%d", active_label, ip4addr_ntoa(&cached_addr), TCP_PORT);

    cyw43_arch_lwip_begin();
    err_t err = tcp_connect(pcb, &cached_addr, TCP_PORT, tcp_connected_cb);
    cyw43_arch_lwip_end();
    if (err != ERR_OK) fail_request("tcp_connect failed");
}

static void dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)name; (void)arg;
    if (!ipaddr) { fail_request("dns lookup failed"); return; }
    cached_addr = *ipaddr;
    have_ip = true;
    klog("thingspeak", "%s: resolved %s -> %s", active_label, cached_host, ip4addr_ntoa(ipaddr));
    begin_connect();
}

// Kicks off one GET /update?api_key=...&field1=..&field2=..&field3=..
// Fails (silently returns) if a request is already in flight -- callers
// (send_room_upload/send_next_lora_upload below) only ever call this
// from task_thingspeak(), itself gated on client_state == CLIENT_IDLE,
// so that's a can't-happen guard, not the normal path. label ("room",
// "probe1"..) is purely for the klog lines below -- lets `cat` of the
// serial console tell which of the two uploads a given line belongs to.
static void start_request(const char *label, const char *host, const char *api_key,
                           float f1, float f2, float f3)
{
    if (client_state != CLIENT_IDLE) return;

    strncpy(active_label, label, sizeof(active_label) - 1);
    active_label[sizeof(active_label) - 1] = '\0';

    request_len = snprintf(request, sizeof(request),
        "GET /update?api_key=%s&field1=%.1f&field2=%.1f&field3=%.1f HTTP/1.1\r\n"
        "Host: %s\r\nConnection: close\r\n\r\n",
        api_key, f1, f2, f3, host);
    sent_len = 0;
    response_logged = false;
    status = THINGSPEAK_SENDING;
    // api_key logged in full, same call as wifi.cpp logging /dev/config/wifi_pass
    // in plain text on purpose -- this is a local debug console, not a remote log.
    klog("thingspeak", "%s: sending to %s (api=%s) field1=%.1f field2=%.1f field3=%.1f",
         active_label, host, api_key, f1, f2, f3);

    if (have_ip && strcmp(cached_host, host) == 0) {
        client_state = CLIENT_CONNECTING; // begin_connect() below sets this again, harmless
        begin_connect();
        return;
    }

    have_ip = false;
    strncpy(cached_host, host, sizeof(cached_host) - 1);
    cached_host[sizeof(cached_host) - 1] = '\0';

    client_state = CLIENT_RESOLVING;
    err_t err = dns_gethostbyname(cached_host, &cached_addr, dns_found_cb, NULL);
    if (err == ERR_OK) {
        have_ip = true; // already cached in lwIP's own resolver, callback won't fire
        klog("thingspeak", "%s: resolved %s -> %s (cached)", active_label, cached_host, ip4addr_ntoa(&cached_addr));
        begin_connect();
    } else if (err != ERR_INPROGRESS) {
        fail_request("dns_gethostbyname failed to start");
    }
    // else: wait for dns_found_cb
}

// ─── the scheduler ─────────────────────────────────────────────────────
static void send_room_upload(void)
{
    char host[40], api[24];
    read_field("/dev/config/url",  host, sizeof(host));
    read_field("/dev/config/api0", api,  sizeof(api));
    if (host[0] == '\0' || api[0] == '\0') {
        klog("thingspeak", "room upload skipped: /dev/config/url or /dev/config/api0 not set");
        return;
    }

    char buf[16];
    read_field("/dev/room/temp",  buf, sizeof(buf)); float t = atof(buf);
    read_field("/dev/room/hum",   buf, sizeof(buf)); float h = atof(buf);
    read_field("/dev/room/light", buf, sizeof(buf)); float l = atof(buf);

    start_request("room", host, api, t, h, l);
}

static void send_next_lora_upload(void)
{
    char host[40];
    read_field("/dev/config/url", host, sizeof(host));
    if (host[0] == '\0') {
        klog("thingspeak", "lora upload skipped: /dev/config/url not set");
        return;
    }

    for (int tries = 0; tries < LORA_PROBE_COUNT; tries++) {
        int probe = lora_probe_rr + 1; // 1..3
        lora_probe_rr = (lora_probe_rr + 1) % LORA_PROBE_COUNT;

        char path[32], seen[4];
        snprintf(path, sizeof(path), "/dev/lora/probe%d/seen", probe);
        read_field(path, seen, sizeof(seen));
        if (seen[0] != '1') continue; // never seen a packet from this probe -- nothing real to send

        char api[24];
        snprintf(path, sizeof(path), "/dev/config/api%d", probe);
        read_field(path, api, sizeof(api));
        if (api[0] == '\0') continue; // no API key configured for this probe yet

        char buf[16];
        snprintf(path, sizeof(path), "/dev/lora/probe%d/temp", probe); read_field(path, buf, sizeof(buf)); float t = atof(buf);
        snprintf(path, sizeof(path), "/dev/lora/probe%d/soil", probe); read_field(path, buf, sizeof(buf)); float s = atof(buf);
        snprintf(path, sizeof(path), "/dev/lora/probe%d/ph",   probe); read_field(path, buf, sizeof(buf)); float p = atof(buf);

        char label[16];
        snprintf(label, sizeof(label), "probe%d", probe);
        start_request(label, host, api, t, s, p);
        return;
    }
    klog("thingspeak", "lora upload skipped: no probe has reported yet (or no api key set)");
}

void task_thingspeak(void)
{
    if (!wifi_is_up()) return;         // no point even trying
    if (client_state != CLIENT_IDLE) return; // previous upload still in flight

    if (!timers_seeded) {
        // First upload of each kind happens one interval after boot, not
        // immediately -- same as the original's own interval_time seed,
        // and it gives room.cpp/lora.cpp a chance to have real data cached.
        next_room_upload = make_timeout_time_ms(ROOM_UPLOAD_INTERVAL_MS);
        next_lora_upload = make_timeout_time_ms(LORA_UPLOAD_OFFSET_MS);
        timers_seeded = true;
        return;
    }

    absolute_time_t now = get_absolute_time();

    if (absolute_time_diff_us(now, next_room_upload) <= 0) {
        send_room_upload();
        next_room_upload = make_timeout_time_ms(ROOM_UPLOAD_INTERVAL_MS);
        return; // one upload started per tick, keep it simple
    }

    if (absolute_time_diff_us(now, next_lora_upload) <= 0) {
        send_next_lora_upload();
        next_lora_upload = make_timeout_time_ms(LORA_UPLOAD_INTERVAL_MS);
    }
}
