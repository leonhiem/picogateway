/**
 * buttons.cpp — button GPIO init + debounce, carved out of gateway.cpp
 *
 * Behavior matches gateway.cpp's repeating_timer_callback exactly, with
 * one fix: the loop bound was `sizeof(button)` there, which only ever
 * happened to equal BUTTON_COUNT because `bool` is 1 byte -- a latent
 * bug (see gateway.cpp review), fixed here by counting buttons
 * explicitly. gateway.cpp itself is left untouched; this is the
 * carved-out, corrected version that /dev/buttons (dev/buttons.cpp)
 * will read from going forward.
 *
 * The old GPIO edge-interrupt hook on ENTER (gateway.cpp's inter_enter)
 * is dropped -- its body was already fully commented out, doing nothing
 * but bumping a timestamp. The repeating-timer debounce below is the
 * only thing actually driving button[] there or here.
 */
#include "buttons.h"
#include "hardware/gpio.h"

const uint button_pin[BUTTON_COUNT] = { BUTTON_PIN_UP,
                                         BUTTON_PIN_DOWN,
                                         BUTTON_PIN_LEFT,
                                         BUTTON_PIN_RIGHT,
                                         BUTTON_PIN_ENTER };

volatile bool button[BUTTON_COUNT];
volatile bool button_pressed[BUTTON_COUNT];
volatile int  button_cnt[BUTTON_COUNT];

void buttons_init(void)
{
    for (int i = 0; i < BUTTON_COUNT; i++) {
        gpio_init(button_pin[i]);
        gpio_pull_up(button_pin[i]);
    }
}

bool repeating_timer_callback(struct repeating_timer *t)
{
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (gpio_get(button_pin[i]) == false && !button_pressed[i] && button_cnt[i] == 0) {
            button_pressed[i] = true;
            button[i] = true;
            button_cnt[i] = DEBOUNCE_SHORT;
        } else if (gpio_get(button_pin[i]) == true && button_pressed[i] && button_cnt[i] == 0) {
            button_pressed[i] = false;
            button_cnt[i] = DEBOUNCE_SHORT;
        } else if (button_cnt[i] > 0) {
            button_cnt[i]--;
        }
    }
    return true;
}

bool any_button_pressed(void)
{
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (button[i]) return true;
    }
    return false;
}

void reset_all_buttons(void)
{
    for (int i = 0; i < BUTTON_COUNT; i++) {
        button[i] = false;
    }
}
