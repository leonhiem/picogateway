/**
 * room.cpp — room sensor GPIO/ADC/DHT init + periodic read, carved out
 * of gateway.cpp
 *
 * task_poll_room() does exactly what gateway.cpp's main loop did every
 * SENSOR_INTERVAL_TIME (2 minutes, not the 1 minute this was
 * misremembered as -- see gateway.cpp: `#define SENSOR_INTERVAL_TIME
 * (60*2*1000)`), minus everything that wasn't the sensor read itself
 * (no run_tcp_client, no display, no reboot counter -- those are
 * separate concerns for later steps).
 *
 * One difference from gateway.cpp's behavior: on a bad DHT read
 * (timeout/checksum), gateway.cpp zeroed temperature_c/humidity, so a
 * single flaky read briefly reported "0.0 C" as if the room had gone
 * to freezing. Here the previous cached reading is left in place
 * instead -- a stale-but-plausible value beats a wrong-but-fresh one
 * for something read only every couple of minutes.
 */
#include "room.h"
#include "hardware/adc.h"
#include <dht.h>

static const dht_model_t ROOM_DHT_MODEL = DHT22;
static dht_t dht;

volatile float room_temp_c    = 0.0f;
volatile float room_hum_pct   = 0.0f;
volatile float room_light_pct = 0.0f;

// Matches gateway.cpp's light conversion exactly: raw ADC counts -> %,
// scaled against the observed real-world max (the light sensor's LM358
// opamp isn't rail-to-rail, so 0xfff/full-scale never actually happens
// -- see gateway.cpp's comment + https://wiki.seeedstudio.com/Grove-Light_Sensor/).
static const float LIGHT_CONVERSION = 100.0f / 0x9ff;

void room_init(void)
{
    adc_init();
    adc_gpio_init(ROOM_LIGHT_ADC_PIN);
    dht_init(&dht, ROOM_DHT_MODEL, pio0, ROOM_DHT_DATA_PIN, true /* pull_up */);
}

void task_poll_room(void)
{
    dht_start_measurement(&dht);
    float humidity, temperature_c;
    dht_result_t result = dht_finish_measurement_blocking(&dht, &humidity, &temperature_c);
    if (result == DHT_RESULT_OK) {
        room_temp_c  = temperature_c;
        room_hum_pct = humidity;
    }
    // DHT_RESULT_TIMEOUT / DHT_RESULT_BAD_CHECKSUM: leave the cache as
    // it was -- see header comment above.

    adc_select_input(ROOM_LIGHT_ADC_INPUT);
    uint16_t adc_result = adc_read();
    room_light_pct = adc_result * LIGHT_CONVERSION;
}
