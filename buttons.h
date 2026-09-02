/**
 * buttons.h — picogateway's 5 front-panel buttons: pins, indices, raw state
 *
 * Same shape as ../picoos's warmer.h button section, sized for this
 * board's 5 buttons instead of 6. Values match gateway.cpp's own
 * #defines exactly (BUTTON_UP..BUTTON_ENTER, BUTTON_PIN_UP..
 * BUTTON_PIN_ENTER) -- this is the same physical board, just the pins
 * pulled out into a reusable header instead of buried in one big
 * main-loop file.
 */
#pragma once

#include "pico/stdlib.h"

#define DEBOUNCE_SHORT 5

#define BUTTON_UP    0
#define BUTTON_DOWN  1
#define BUTTON_LEFT  2
#define BUTTON_RIGHT 3
#define BUTTON_ENTER 4

#define BUTTON_PIN_UP    11
#define BUTTON_PIN_DOWN  12
#define BUTTON_PIN_LEFT  13
#define BUTTON_PIN_RIGHT 14
#define BUTTON_PIN_ENTER 6

#define BUTTON_COUNT 5

extern const uint button_pin[BUTTON_COUNT];

// Debounced state, written from repeating_timer_callback (interrupt
// context) and read from cooperative task/shell context -- same
// single-core-cooperative safety argument as kernel/klog.h. See
// dev/buttons.cpp for the read-and-clear semantics built on these.
extern volatile bool button[BUTTON_COUNT];
extern volatile bool button_pressed[BUTTON_COUNT];
extern volatile int  button_cnt[BUTTON_COUNT];

// GPIO init only -- does not start the debounce timer itself; call
// add_repeating_timer_ms(-5, repeating_timer_callback, NULL, &timer)
// from main() same as gateway.cpp does today.
void buttons_init(void);

bool repeating_timer_callback(struct repeating_timer *t);

bool any_button_pressed(void);
void reset_all_buttons(void);

// dev/buttons.cpp -- registers /dev/buttons and /dev/buttons/<name>
// with kernel/fs.h. Call once from main(), after buttons_init().
void buttons_register(void);
void button_devices_register(void);
