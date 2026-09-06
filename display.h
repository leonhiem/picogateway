/**
 * display.h — the SSD1306 OLED: hardware bring-up + screen mode + menu
 *
 * Same split as buttons.h/room.h/lora.h/wifi.h: this header (backed by
 * display.cpp) owns the hardware and all the interactive state;
 * dev/display.cpp turns the persistent bits into /dev/display/*;
 * prog/display.cpp (bin/display) is the only thing that actually starts
 * it, from the boot script -- gateway.cpp itself never touches the I2C
 * bus this runs on.
 *
 * Unlike ../picoos's ST7735 driver, "banner" and "menu" aren't two
 * independent bin/ programs racing to draw into the same framebuffer --
 * one task (task_display) owns the display exclusively and dispatches
 * internally on screen_mode, the same way picoos's tft_flush_task
 * dispatches on graphical/text. And unlike picoos's tft (which mirrors
 * *other* devices' state, sourced by a separate killable job,
 * bin/tftwire), the menu's cursor position and which screen is on-screen
 * don't exist anywhere except as display state -- so navigation and
 * rendering stay together in this one task instead of being split into a
 * wiring job + flush task. Killing the thing that reads your button
 * presses mid-menu would be a worse experience, not a more Unix-y one.
 */
#pragma once

#include "pico/stdlib.h"

typedef enum { DISPLAY_MODE_BANNER, DISPLAY_MODE_MENU } display_mode_t;

// Non-blocking, rate-limited hardware bring-up (i2c1 + ssd1306_init()) --
// same shape as lora_try_start()/wifi_arch_ready(). Returns true once the
// display is actually up (immediately, on every call after the first
// success). Called only from bin/display (prog/display.cpp), backgrounded
// from the boot script -- nothing else ever touches the I2C bus this runs
// on before that.
bool display_try_start(void);

// True once display_try_start() has succeeded. What /dev/display/status
// (dev/display.cpp) reports.
bool display_is_ready(void);

display_mode_t display_get_mode(void);
void display_set_mode(display_mode_t mode);

// Reads /dev/buttons/{up,down,enter}, advances the menu state machine if
// in menu mode, redraws. No-ops until display_is_ready() -- safe to
// register unconditionally at boot (gateway.cpp), same as
// task_poll_lora. Run this often (e.g. every 50ms) so button presses
// feel instant to the user.
void task_display(void);

// dev/display.cpp -- registers /dev/display/mode, /dev/display/status.
// Call once from main().
void display_devices_register(void);
