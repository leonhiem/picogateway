/**
 * eeprom.h — the 24CS16 config EEPROM: raw chip I/O, carved out of
 * gateway.cpp's eeprom_write_small/eeprom_write/eeprom_read_small/
 * eeprom_read (same I2C mechanics, same page/offset layout -- see
 * below), plus what gateway.cpp never did: verifying a write actually
 * landed, and (dev/config.cpp) validating a read before trusting it.
 *
 * Page/offset layout is pinned to match gateway.cpp's own
 * EEPROM_OFFS_* constants exactly, on purpose: this is the same
 * physical chip a real gateway.cpp unit has already been writing to,
 * and a chip it already populated should read back the same fields
 * here.
 */
#pragma once

#include "pico/stdlib.h"

#define EEPROM_DEV_ADDR 0x50 // matches gateway.cpp
#define EEPROM_WR_PIN   17   // matches gateway.cpp -- write-protect gate

// Each config field gets one 32-byte slot -- matches the spacing
// gateway.cpp's EEPROM_OFFS_* constants already use (WifiSSID=32,
// WifiPass=64, URL=96, FARM=128 on page 0; API0..API3=0,32,64,96 on
// page 1), so slots never overlap regardless of how much of each one
// is actually used.
#define EEPROM_SLOT_SIZE 32

// Same offsets gateway.cpp defines (its own #defines are private to
// gateway.cpp, not included from here -- gateway.cpp stays untouched).
#define EEPROM_OFFS_WifiSSID 32
#define EEPROM_OFFS_WifiPass 64
#define EEPROM_OFFS_URL      96
#define EEPROM_OFFS_FARM     128
#define EEPROM_OFFS_API0     0
#define EEPROM_OFFS_API1     32
#define EEPROM_OFFS_API2     64
#define EEPROM_OFFS_API3     96

// I2C0 init + EEPROM_WR_PIN setup, same as gateway.cpp's init block.
void eeprom_init(void);

// Writes exactly EEPROM_SLOT_SIZE bytes at (page, addr), then reads
// them back and compares -- retrying a few times -- until the chip
// actually holds what was sent. gateway.cpp trusted every
// i2c_write_blocking() call blind; this is the write-time half of
// "make sure the EEPROM can't end up holding something other than
// what was asked for". Returns false if every retry still mismatched.
bool eeprom_write_verified(uint8_t page, uint8_t addr, const uint8_t *buf);

// Reads exactly EEPROM_SLOT_SIZE raw bytes at (page, addr) into buf.
// No validation here -- dev/config.cpp's read-side checks (null
// terminator present, printable ASCII) are a separate, deliberate
// step; this is just the chip access.
void eeprom_read_raw(uint8_t page, uint8_t addr, uint8_t *buf);

// dev/config.cpp -- registers /dev/config/wifi_ssid, wifi_pass, url,
// farm, api0..api3. Call once from main(), after eeprom_init().
void config_register(void);
