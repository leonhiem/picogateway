/**
 * eeprom.cpp — 24CS16 raw I2C access, carved out of gateway.cpp, plus
 * write verification gateway.cpp never had
 *
 * eeprom_write_chunked/eeprom_read_chunked below are gateway.cpp's own
 * eeprom_write/eeprom_write_small/eeprom_read/eeprom_read_small,
 * unchanged in mechanics (same 8-byte-at-a-time chunking, same
 * write-protect GPIO toggle + settle delays -- the 24C16 only accepts
 * up to 8 bytes per page write) with the printf tracing dropped.
 */
#include "eeprom.h"
#include "hardware/i2c.h"
#include <cstring>

#define EEPROM_WRITE_RETRIES 3

static void eeprom_write_chunk(const uint8_t page, const uint8_t addr,
                                const uint8_t *buf, const uint8_t nbytes)
{
    uint8_t device_address = EEPROM_DEV_ADDR | page;
    uint8_t msg[nbytes + 1];
    msg[0] = addr;
    for (int i = 0; i < nbytes; i++) msg[i + 1] = buf[i];

    gpio_put(EEPROM_WR_PIN, 0); // WR protection OFF
    sleep_ms(20);
    i2c_write_blocking(i2c0, device_address, msg, nbytes + 1, false);
    sleep_ms(20); // EEPROM write takes some time
    gpio_put(EEPROM_WR_PIN, 1); // WR protection ON
}

static void eeprom_write_chunked(const uint8_t page, uint8_t addr,
                                  const uint8_t *buf, uint8_t nbytes)
{
    uint8_t idx = 0;
    uint8_t remain = nbytes < 8 ? nbytes : 8;
    while (nbytes > 0) {
        eeprom_write_chunk(page, addr, &buf[idx], remain);
        nbytes -= remain;
        addr += remain;
        idx += remain;
        if (nbytes < 8) remain = nbytes;
    }
}

static void eeprom_read_chunk(const uint8_t page, const uint8_t addr,
                               uint8_t *buf, const uint8_t nbytes)
{
    uint8_t device_address = EEPROM_DEV_ADDR | page;
    i2c_write_blocking(i2c0, device_address, &addr, 1, true);
    i2c_read_blocking(i2c0, device_address, buf, nbytes, false);
}

static void eeprom_read_chunked(const uint8_t page, uint8_t addr,
                                 uint8_t *buf, uint8_t nbytes)
{
    uint8_t idx = 0;
    uint8_t remain = nbytes < 8 ? nbytes : 8;
    while (nbytes > 0) {
        eeprom_read_chunk(page, addr, &buf[idx], remain);
        nbytes -= remain;
        addr += remain;
        idx += remain;
        if (nbytes < 8) remain = nbytes;
    }
}

void eeprom_init(void)
{
    i2c_init(i2c0, 200000);
    gpio_set_function(20, GPIO_FUNC_I2C);
    gpio_set_function(21, GPIO_FUNC_I2C);

    gpio_init(EEPROM_WR_PIN);
    gpio_set_dir(EEPROM_WR_PIN, GPIO_OUT);
    gpio_put(EEPROM_WR_PIN, 1); // 1: write not allowed
}

void eeprom_read_raw(uint8_t page, uint8_t addr, uint8_t *buf)
{
    eeprom_read_chunked(page, addr, buf, EEPROM_SLOT_SIZE);
}

bool eeprom_write_verified(uint8_t page, uint8_t addr, const uint8_t *buf)
{
    uint8_t check[EEPROM_SLOT_SIZE];

    for (int attempt = 0; attempt < EEPROM_WRITE_RETRIES; attempt++) {
        eeprom_write_chunked(page, addr, buf, EEPROM_SLOT_SIZE);
        eeprom_read_chunked(page, addr, check, EEPROM_SLOT_SIZE);
        if (memcmp(buf, check, EEPROM_SLOT_SIZE) == 0) return true;
    }
    return false;
}
