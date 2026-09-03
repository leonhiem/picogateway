/**
 * lora.h — the 3 field probes talking to this gateway over LoRa
 *
 * "Probe" (not "sensor1"/"slave1") because that's what each remote
 * unit actually is: a battery-powered soil probe that wakes up,
 * transmits whatever it has, and goes back to sleep -- on its own
 * schedule, not polled by us. There are up to LORA_PROBE_COUNT of
 * them (ids 1..3, matching gateway.cpp's own sensor_id range), and any
 * one of them may simply never transmit -- that's normal, not an
 * error, so nothing here assumes a probe will ever be heard from.
 *
 * Wire format is unchanged from gateway.cpp: a packet is
 * "=<id>&field1=<temp>&field2=<soil>&field3=<ph>" where <id> is the
 * ASCII digit '1'..'3' and the three values are ASCII decimal numbers.
 * See lora.cpp's parser for the (more bounds-checked) read of it.
 *
 * Same split as buttons.h/room.h/eeprom.h: this header owns the cache,
 * lora.cpp owns the radio and the periodic task that drives it,
 * dev/lora.cpp turns the cache into /dev/lora/probe<n>/*.
 */
#pragma once

#include "pico/stdlib.h"

#define LORA_PROBE_COUNT 3 // ids 1..3; index i here is probe (i+1)

// Cached last packet per probe, written only from task_poll_lora, read
// from dev/lora.cpp's device read()s -- same single-core-cooperative
// safety argument as buttons.h/room.h. probe_seen[i] is false until
// the first packet from that probe ever arrives -- the explicit way
// to tell "never heard from" apart from "reported 0.0", which a bare
// cached float can't do on its own. temp/soil/ph/rssi/age all read as
// plain numbers (0.0/0/-1 before the first packet) so anything reading
// them doesn't have to special-case a non-numeric value -- /dev/lora/
// probe<n>/seen (dev/lora.cpp) is the device to check first if that
// distinction matters to the reader.
extern volatile bool  probe_seen[LORA_PROBE_COUNT];
extern volatile float probe_temp[LORA_PROBE_COUNT];
extern volatile float probe_soil[LORA_PROBE_COUNT];
extern volatile float probe_ph[LORA_PROBE_COUNT];
extern volatile int   probe_rssi[LORA_PROBE_COUNT];
extern volatile uint32_t probe_last_seen_ms[LORA_PROBE_COUNT]; // to_ms_since_boot() at last packet

// Starts the radio (LoRa.begin(), retried until it succeeds -- same
// open-ended retry gateway.cpp does at boot, since without a radio
// there's nothing else useful this subsystem can do).
void lora_init(void);

// Polls LoRa.parsePacket()/available() and, on a complete packet,
// parses and caches it. Run this often (e.g. every 50ms) from
// kernel/task.h -- parsePacket() is a quick register read, not a
// blocking wait, so a short period costs little. On a malformed
// packet (bad length, wrong format), re-runs LoRa.begin() the same
// way gateway.cpp's own recovery path does -- see lora.cpp.
void task_poll_lora(void);

// dev/lora.cpp -- registers /dev/lora/probe<1..3>/{temp,soil,ph,rssi,age,seen}.
// Call once from main(), after lora_init().
void lora_devices_register(void);
