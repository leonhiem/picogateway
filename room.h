/**
 * room.h — picogateway's room sensors: DHT22 (temp+hum) + light ADC
 *
 * Same split as buttons.h/buttons.cpp: this header owns the hardware
 * (pins, cached readings), room.cpp does the actual sensor access,
 * dev/room.cpp turns the cache into /dev/room/temp, /dev/room/hum,
 * /dev/room/light.
 *
 * Unlike buttons (debounced on a 5ms hardware timer -- needs interrupt
 * regularity), the DHT22 read is a several-millisecond blocking library
 * call that must not be hammered (gateway.cpp only ever did it once
 * every SENSOR_INTERVAL_TIME = 2 minutes), so task_poll_room is a plain
 * kernel/task.h task, not a timer ISR -- same shape as picoos's
 * tpo_apply/tft_flush_task: real hardware work run periodically by the
 * cooperative scheduler itself, no fs.h round-trip needed to drive it.
 *
 * /dev/room/* devices then just format whatever task_poll_room last
 * cached -- instant, non-blocking reads, same as /dev/buttons/<name>.
 */
#pragma once

#include "pico/stdlib.h"

#define ROOM_DHT_DATA_PIN   15 // matches gateway.cpp's DATA_PIN
#define ROOM_LIGHT_ADC_PIN  27
#define ROOM_LIGHT_ADC_INPUT 1

// Cached last-read values, written only from task_poll_room, read from
// dev/room.cpp's device read()s -- same single-core-cooperative safety
// argument as buttons.h. Read 0.0 until the first successful poll (see
// task_poll_room's comment) -- there's no separate "valid" flag, same
// as picoos's dev/percent.cpp just seeding a plain default.
extern volatile float room_temp_c;
extern volatile float room_hum_pct;
extern volatile float room_light_pct;

// GPIO/ADC/DHT init only -- does not start polling itself; register
// task_poll_room with kernel/task.h from main(), same as gateway.cpp's
// SENSOR_INTERVAL_TIME-gated read but now scheduler-driven instead of
// hand-rolled.
void room_init(void);
void task_poll_room(void);

// dev/room.cpp -- registers /dev/room/temp, /dev/room/hum,
// /dev/room/light with kernel/fs.h. Call once from main(), after
// room_init().
void room_devices_register(void);
