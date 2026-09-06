/**
 * display.cpp — SSD1306 bring-up + the menu, carved out of gateway.cpp's
 * old animation_init()/animation_wifi()/animation() and rebuilt on
 * /dev/*
 *
 * Layout is deliberately the original's own style: a title line at y=0,
 * then up to four content lines at y=12,24,36,48 (scale 1) -- that's
 * exactly what fits on this 128x64 panel (floor(64/12) == 5 rows total).
 * The first line always says what you're looking at, same as the
 * original's "------- menu -------"/"------- sensors -----" titles.
 * Selected items get their line's first character swapped for '>',
 * exactly like the original's `main_menu[idx][0] = '>'` trick, just
 * built into draw_list() below instead of mutating a stored string.
 *
 * The screens themselves are a small, explicit state machine (menu_screen_t
 * + a switch per screen), same shape as the original's if/else chain on
 * selected_menu -- not a generic data-driven engine. What's different is
 * *how* each screen gets its data: every value on screen is read live
 * through kernel/fs.h (fs_open/fs_read on /dev/room/*, /dev/lora/*,
 * /dev/wifi/status, /dev/config/farm), never a hand-rolled array indexed
 * by a menu constant -- there is no apis[4][22]-style buffer here at all,
 * so there's no arithmetic on a menu constant that can land one off and
 * write out of bounds (see gateway.cpp's own header comment, and the
 * memory note this bug is filed under). Editing those fields is step 13;
 * this step is read-only.
 *
 * Button reads go through /dev/buttons/{up,down,enter} (fs_open/fs_read),
 * same idiom prog/toggle.cpp and prog/follow.cpp already use -- this
 * file is a *consumer* of button events, not buttons.h's owner, so it
 * goes through the device layer like any other consumer would.
 *
 * Redraws are dirty-flag gated in menu mode (only a button press sets
 * needs_refresh, same as the original's display_needs_refresh), but
 * banner mode redraws on its own short timer instead -- unlike the old
 * banner (drawn once at boot, before wifi ever tried to connect), this
 * one shows live /dev/wifi/status and /dev/lora/status, which change on
 * their own in the background now that both are non-blocking bin/
 * jobs -- so it needs to keep polling, not just draw once.
 */
#include "display.h"
#include "ssd1306.h"
#include "kernel/fs.h"
#include "hardware/i2c.h"
#include <cstdio>
#include <cstring>

#define OLED_I2C      i2c1
#define OLED_SDA_PIN  2
#define OLED_SCL_PIN  3
#define OLED_ADDR     0x3C
#define OLED_WIDTH    128
#define OLED_HEIGHT   64

#define DISPLAY_RETRY_MS 3000 // rate limit for display_try_start()'s
                               // retries, same shape/reasoning as
                               // lora.cpp's LORA_RETRY_MS

static ssd1306_t disp;
static bool      ready = false;
static uint32_t  last_attempt_ms = 0;

static display_mode_t mode = DISPLAY_MODE_BANNER;

#define BANNER_REDRAW_MS 1000
static absolute_time_t banner_next_redraw;

// Detail screens (room/wifi/lora status/probes) show live values that
// change on their own in the background (room sensors every 10s,
// wifi/lora whenever their state changes) -- unlike the list screens,
// whose item labels never change on their own, these need to redraw
// periodically even with no button pressed, or the numbers on screen go
// stale until the next navigation. List screens don't get this: their
// content only ever changes in response to up/down/enter.
#define DETAIL_REFRESH_MS 1000
static absolute_time_t detail_next_redraw;

// ─── screens ──────────────────────────────────────────────────────────
typedef enum {
    SCR_MAIN,
    SCR_ROOM,
    SCR_LORA_LIST,
    SCR_LORA_STATUS,
    SCR_LORA_PROBE1,
    SCR_LORA_PROBE2,
    SCR_LORA_PROBE3,
    SCR_WIFI,
} menu_screen_t;

static menu_screen_t screen = SCR_MAIN;
static int  cursor = 0;          // selected item within SCR_MAIN/SCR_LORA_LIST
static bool needs_refresh = true;

static const char *MAIN_ITEMS[3] = {"Room sensors", "LoRa probes", "Wifi status"};
static const char *LORA_ITEMS[4] = {"Status", "Probe 1", "Probe 2", "Probe 3"};

// ─── hardware bring-up ────────────────────────────────────────────────
bool display_try_start(void)
{
    if (ready) return true;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (last_attempt_ms != 0 && now - last_attempt_ms < DISPLAY_RETRY_MS) return false;
    last_attempt_ms = now;

    i2c_init(OLED_I2C, 400000);
    gpio_set_function(OLED_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(OLED_SCL_PIN, GPIO_FUNC_I2C);

    disp.external_vcc = false;
    if (!ssd1306_init(&disp, OLED_WIDTH, OLED_HEIGHT, OLED_ADDR, OLED_I2C)) return false;

    ssd1306_clear(&disp);
    ssd1306_show(&disp);
    banner_next_redraw = get_absolute_time();
    ready = true;
    return true;
}

bool display_is_ready(void) { return ready; }

display_mode_t display_get_mode(void) { return mode; }

void display_set_mode(display_mode_t m)
{
    mode = m;
    needs_refresh = true;
    banner_next_redraw = get_absolute_time(); // so switching back to banner draws immediately
}

// ─── small helpers ────────────────────────────────────────────────────
// Reads one device into a nul-terminated, trailing-newline-stripped
// string. Leaves out empty on any failure -- every caller below just
// shows whatever comes back, blank included, rather than needing its
// own error path.
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

static bool button_down(const char *path)
{
    char buf[4];
    read_field(path, buf, sizeof(buf));
    return buf[0] == '1';
}

static void draw_title(const char *title)
{
    ssd1306_draw_string(&disp, 0, 0, 1, title);
}

// Up to 4 items, one per content row, selected item's line starts with
// '>' instead of ' ' -- the original's cursor trick, without mutating a
// stored string to do it.
static void draw_list(const char *const *items, int count, int sel)
{
    for (int i = 0; i < count && i < 4; i++) {
        char line[24];
        snprintf(line, sizeof(line), "%c %s", i == sel ? '>' : ' ', items[i]);
        ssd1306_draw_string(&disp, 0, 12 * (i + 1), 1, line);
    }
}

// One "label: value" content row, value read live from a device.
static void draw_field(int row, const char *label, const char *path)
{
    char val[20];
    read_field(path, val, sizeof(val));
    char line[24];
    snprintf(line, sizeof(line), "%s: %s", label, val[0] ? val : "?");
    ssd1306_draw_string(&disp, 0, 12 * row, 1, line);
}

static void draw_probe_screen(int probe_n) // 1..3
{
    char title[24], path[32];
    snprintf(title, sizeof(title), "---- lora probe %d ----", probe_n);
    draw_title(title);
    snprintf(path, sizeof(path), "/dev/lora/probe%d/temp", probe_n); draw_field(1, "Temp", path);
    snprintf(path, sizeof(path), "/dev/lora/probe%d/soil", probe_n); draw_field(2, "Soil", path);
    snprintf(path, sizeof(path), "/dev/lora/probe%d/ph",   probe_n); draw_field(3, "pH",   path);
    snprintf(path, sizeof(path), "/dev/lora/probe%d/seen", probe_n); draw_field(4, "Seen", path);
}

// ─── redraw ────────────────────────────────────────────────────────────
static void redraw_banner(void)
{
    ssd1306_clear(&disp);
    char farm[20];
    read_field("/dev/config/farm", farm, sizeof(farm));

    ssd1306_draw_string(&disp, 8, 0,  2, "Hello");
    ssd1306_draw_string(&disp, 8, 16, 2, farm[0] ? farm : "farm");

    char wifi[20], lora[20], line[24];
    read_field("/dev/wifi/status", wifi, sizeof(wifi));
    read_field("/dev/lora/status", lora, sizeof(lora));
    snprintf(line, sizeof(line), "wifi: %s", wifi[0] ? wifi : "?");
    ssd1306_draw_string(&disp, 0, 36, 1, line);
    snprintf(line, sizeof(line), "lora: %s", lora[0] ? lora : "?");
    ssd1306_draw_string(&disp, 0, 48, 1, line);

    ssd1306_show(&disp);
}

static void redraw_menu(void)
{
    ssd1306_clear(&disp);
    switch (screen) {
        case SCR_MAIN:
            draw_title("------- menu -------");
            draw_list(MAIN_ITEMS, 3, cursor);
            break;
        case SCR_LORA_LIST:
            draw_title("------- lora -------");
            draw_list(LORA_ITEMS, 4, cursor);
            break;
        case SCR_ROOM:
            draw_title("---- room sensors ----");
            draw_field(1, "Temp",  "/dev/room/temp");
            draw_field(2, "Hum",   "/dev/room/hum");
            draw_field(3, "Light", "/dev/room/light");
            break;
        case SCR_WIFI:
            draw_title("------- wifi -------");
            draw_field(1, "Status", "/dev/wifi/status");
            break;
        case SCR_LORA_STATUS:
            draw_title("---- lora status ----");
            draw_field(1, "Status", "/dev/lora/status");
            break;
        case SCR_LORA_PROBE1: draw_probe_screen(1); break;
        case SCR_LORA_PROBE2: draw_probe_screen(2); break;
        case SCR_LORA_PROBE3: draw_probe_screen(3); break;
    }
    ssd1306_show(&disp);
}

// ─── input / navigation ────────────────────────────────────────────────
// SCR_LORA_STATUS..SCR_LORA_PROBE3 are contiguous on purpose -- lets
// up/down cycle through them with plain modular arithmetic, same trick
// the original used on its own MENU_SENSOR_0..3 run of constants.
#define LORA_DETAIL_COUNT 4
static void lora_detail_cycle(int delta)
{
    int idx = (int)screen - (int)SCR_LORA_STATUS;
    idx = (idx + delta + LORA_DETAIL_COUNT) % LORA_DETAIL_COUNT;
    screen = (menu_screen_t)(SCR_LORA_STATUS + idx);
}

static void handle_menu_input(bool up, bool down, bool enter)
{
    switch (screen) {
        case SCR_MAIN:
            if (enter) {
                screen = (cursor == 0) ? SCR_ROOM : (cursor == 1) ? SCR_LORA_LIST : SCR_WIFI;
                needs_refresh = true;
            } else if (up)   { cursor = (cursor + 2) % 3; needs_refresh = true; }
            else if (down) { cursor = (cursor + 1) % 3; needs_refresh = true; }
            break;

        case SCR_LORA_LIST:
            if (enter) {
                screen = (menu_screen_t)(SCR_LORA_STATUS + cursor);
                needs_refresh = true;
            } else if (up)   { cursor = (cursor + 3) % 4; needs_refresh = true; }
            else if (down) { cursor = (cursor + 1) % 4; needs_refresh = true; }
            break;

        case SCR_ROOM:
        case SCR_WIFI:
            if (enter) { screen = SCR_MAIN; needs_refresh = true; }
            break;

        case SCR_LORA_STATUS:
        case SCR_LORA_PROBE1:
        case SCR_LORA_PROBE2:
        case SCR_LORA_PROBE3:
            // Same touch the original had on MENU_SENSOR_0..3: while
            // looking at one detail screen, up/down jumps straight to
            // the next/previous sibling instead of returning to the
            // list first.
            if (enter)     { screen = SCR_MAIN; cursor = 1; needs_refresh = true; }
            else if (up)   { lora_detail_cycle(-1); needs_refresh = true; }
            else if (down) { lora_detail_cycle(+1); needs_refresh = true; }
            break;
    }
}

static bool is_detail_screen(menu_screen_t s)
{
    switch (s) {
        case SCR_ROOM:
        case SCR_WIFI:
        case SCR_LORA_STATUS:
        case SCR_LORA_PROBE1:
        case SCR_LORA_PROBE2:
        case SCR_LORA_PROBE3:
            return true;
        default:
            return false; // SCR_MAIN, SCR_LORA_LIST -- labels never change on their own
    }
}

void task_display(void)
{
    if (!ready) return;

    bool up    = button_down("/dev/buttons/up");
    bool down  = button_down("/dev/buttons/down");
    bool enter = button_down("/dev/buttons/enter");

    if (mode == DISPLAY_MODE_BANNER) {
        if (up || down || enter) {
            display_set_mode(DISPLAY_MODE_MENU); // any button wakes the menu
        } else if (absolute_time_diff_us(get_absolute_time(), banner_next_redraw) <= 0) {
            redraw_banner();
            banner_next_redraw = make_timeout_time_ms(BANNER_REDRAW_MS);
        }
        return;
    }

    handle_menu_input(up, down, enter);

    if (!needs_refresh && is_detail_screen(screen) &&
        absolute_time_diff_us(get_absolute_time(), detail_next_redraw) <= 0) {
        needs_refresh = true; // periodic auto-refresh -- see detail_next_redraw's comment above
    }

    if (needs_refresh) {
        redraw_menu();
        needs_refresh = false;
        detail_next_redraw = make_timeout_time_ms(DETAIL_REFRESH_MS);
    }
}
