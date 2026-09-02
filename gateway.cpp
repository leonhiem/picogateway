/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
# Test session with telnet:

leon@x1:~/pico/LoRa-pi-pico$ telnet api.thingspeak.com 80
Trying 34.193.28.232...
Connected to api.thingspeak.com.
Escape character is '^]'.
GET /update?api_key=PLGN8IZTXYL2QMFF&field1=0 HTTP/1.1
Host: api.thingspeak.com

HTTP/1.1 200 OK
Date: Wed, 07 Dec 2022 10:44:22 GMT
Content-Type: text/plain; charset=utf-8
Content-Length: 2
Connection: keep-alive
Status: 200 OK
Cache-Control: max-age=0, private, must-revalidate
Access-Control-Allow-Origin: *
Access-Control-Max-Age: 1800
X-Request-Id: 61229642-97e6-4f4e-ad72-2f7230ac04bc
Access-Control-Allow-Headers: origin, content-type, X-Requested-With
Access-Control-Allow-Methods: GET, POST, PUT, OPTIONS, DELETE, PATCH
ETag: W/"6b51d431df5d7f141cbececcf79edf3d"
X-Frame-Options: SAMEORIGIN

12Connection closed by foreign host.
*/



#include <string.h>
#include <time.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include <dht.h>
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/watchdog.h"
#include "hardware/i2c.h"
#include "pico/binary_info.h"
#include "LoRa-RP2040.h"
#include "ssd1306.h"

// change this to match your setup
static const dht_model_t DHT_MODEL = DHT22;
static const uint DATA_PIN = 15;

static float celsius_to_fahrenheit(float temperature) {
    return temperature * (9.0f / 5) + 32;
}


#define INTRO_LOGO "farm"
#define GATEWAY_VERSION "LH20231213"

#define API_SERVER "api.thingspeak.com"



// WIFI_SSID / WIFI_PASSWORD are supplied at configure time, e.g.:
//   cmake -DWIFI_SSID="..." -DWIFI_PASSWORD="..." ..
// (or via CMakeUserPresets.json, which is gitignored) -- see CMakeLists.txt.


// Yen Bai
#define SENSOR_API0 "L3UFA4X5LUNR6OLY"
#define SENSOR_API1 "TXC58K2GWXEJ2K10"
#define SENSOR_API2 "O6KG3WQT8FSL0T31"
#define SENSOR_API3 "LVCC3CP113ZY85NC"
#define FARM_NAME  "Yen Bai"


#define SENSOR_INTERVAL_TIME (60*2*1000)
#define REBOOT_AFTER_ 15

#define TCP_PORT 80
#define DEBUG_printf printf
#define BUF_SIZE 2048

#define TEST_ITERATIONS 10
#define POLL_TIME_S 5

#define EEPROM_CLR  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
//#define EEPROM_CLR  "0123456789abcdefghijklmnopqyz*!@"


#if 0
static void dump_bytes(const uint8_t *bptr, uint32_t len) {
    unsigned int i = 0;

    printf("dump_bytes %d", len);
    for (i = 0; i < len;) {
        if ((i & 0x0f) == 0) {
            printf("\n");
        } else if ((i & 0x07) == 0) {
            printf(" ");
        }
        printf("%02x ", bptr[i++]);
    }
    printf("\n");
}
#define DUMP_BYTES dump_bytes
#else
#define DUMP_BYTES(A,B)
#endif





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

#define EEPROM_DEV_ADDR 0x50
#define EEPROM_WR_PIN 17

//           page offset
// Header:    0   0..31
// Wifi SSID: 0   32..63
// Wifi Pass: 0   64..95
// URL      : 0   96..127
// Farm name: 0   128..159

// API[0]   : 1   0..31
// API[1]   : 1   32..63
// API[2]   : 1   64..95
// API[3]   : 1   96..127
#define EEPROM_OFFS_HEADER   0
#define EEPROM_OFFS_WifiSSID 32
#define EEPROM_OFFS_WifiPass 64
#define EEPROM_OFFS_URL      96
#define EEPROM_OFFS_FARM     128
#define EEPROM_OFFS_API0     0
#define EEPROM_OFFS_API1     32
#define EEPROM_OFFS_API2     64
#define EEPROM_OFFS_API3     96

#define MENU_MAIN        0
#define MENU_SENSOR      10
#define MENU_SENSOR_0    100
#define MENU_SENSOR_1    101
#define MENU_SENSOR_2    102
#define MENU_SENSOR_3    103
#define MENU_WIFI        11
#define MENU_WIFI_PASSWD 110
#define MENU_URL         111
#define MENU_FARM        12
#define MENU_API         13
#define MENU_API_0       130
#define MENU_API_1       131
#define MENU_API_2       132
#define MENU_API_3       133

#define MAX_STR_LEN 22

const uint  button_pin[5] = { BUTTON_PIN_UP,
                              BUTTON_PIN_DOWN,
                              BUTTON_PIN_LEFT,
                              BUTTON_PIN_RIGHT,
                              BUTTON_PIN_ENTER };

void setup_gpios(void);
void animation(void);

// button irq begin
volatile bool button_state;

volatile bool button_pressed[5];
volatile bool button[5];
volatile int  button_cnt[5];
volatile int  any_button_pressed_delay;

// Debounce control
uint32_t time_inter_enter;
const int delayTime = 1000; // Delay for every push button may vary

void inter_enter(uint gpio, uint32_t events) {
    if ((to_ms_since_boot(get_absolute_time())-time_inter_enter)>delayTime) {
        // Recommend to not to change the position of this line
        time_inter_enter = to_ms_since_boot(get_absolute_time());
        
        // Interrupt function lines
        //button_state = !button_state;
        //gpio_put(LED_PIN, button_state);
        //button_event_enter=true;
    }
}
// button irq end


bool any_button_pressed(void)
{
    return (button[BUTTON_UP] ||
            button[BUTTON_DOWN] ||
            button[BUTTON_LEFT] ||
            button[BUTTON_RIGHT] ||
            button[BUTTON_ENTER]);
}

void reset_all_buttons(void) 
{
    button[BUTTON_UP]=false;
    button[BUTTON_DOWN]=false;
    button[BUTTON_LEFT]=false;
    button[BUTTON_RIGHT]=false;
    button[BUTTON_ENTER]=false;
}

volatile bool timer_fired = false;
 
int64_t alarm_callback(alarm_id_t id, void *user_data) {
    printf("Timer %d fired!\n", (int) id);
    timer_fired = true;
    // Can return a value here in us to fire in the future
    return 0;
}
 

bool repeating_timer_callback(struct repeating_timer *t) 
{
    int i;
    for(i=0;i<sizeof(button);i++) {
        if((gpio_get(button_pin[i]) == false) && button_pressed[i]==false && button_cnt[i]==0) {
            button_pressed[i]=true; button[i]=true; button_cnt[i]=DEBOUNCE_SHORT;
            any_button_pressed_delay=5000;
        } else if((gpio_get(button_pin[i]) == true) && button_pressed[i]==true && button_cnt[i]==0) {
            button_pressed[i]=false; button_cnt[i]=DEBOUNCE_SHORT;
        } else if(button_cnt[i]>0) button_cnt[i]--;
    }
    if(any_button_pressed_delay>0) any_button_pressed_delay--;
    return true;
}
 

void setup_gpios(void)
{
    int i;
    // OLED
    i2c_init(i2c1, 400000);
    gpio_set_function(2, GPIO_FUNC_I2C);
    gpio_set_function(3, GPIO_FUNC_I2C);
    gpio_pull_up(2);
    gpio_pull_up(3);

    // EEPROM
    i2c_init(i2c0, 200000);
    gpio_set_function(20, GPIO_FUNC_I2C);
    gpio_set_function(21, GPIO_FUNC_I2C);
    //gpio_pull_up(20);
    //gpio_pull_up(21);
    gpio_init(EEPROM_WR_PIN);
    gpio_set_dir(EEPROM_WR_PIN, GPIO_OUT);
    gpio_put(EEPROM_WR_PIN, 1); // 1: write not allowed

    // Buttons
    gpio_init(BUTTON_PIN_UP);
    gpio_pull_up(BUTTON_PIN_UP);
    gpio_init(BUTTON_PIN_DOWN);
    gpio_pull_up(BUTTON_PIN_DOWN);
    gpio_init(BUTTON_PIN_LEFT);
    gpio_pull_up(BUTTON_PIN_LEFT);
    gpio_init(BUTTON_PIN_RIGHT);
    gpio_pull_up(BUTTON_PIN_RIGHT);
    gpio_init(BUTTON_PIN_ENTER);
    gpio_pull_up(BUTTON_PIN_ENTER);
}

// based on the 24CS16 EEPROM:
// addr should be 7 bit only
// page should be 000..111
// addr EEPROM: 0xA0 >> 1 = 1010 0000 >> 1 = 0101 0000 = 0x50
// addr Serial: 0xB0 >> 1 = 1011 0000 >> 1 = 0101 1000 = 0x58
void eeprom_write_small(i2c_inst_t *i2c, const uint8_t dev_addr,
                  const uint8_t page, const uint8_t addr,
                  uint8_t *buf, const uint8_t nbytes)
{
    uint8_t device_address = dev_addr | page;
    uint8_t msg[nbytes + 1];
    int i,ret;
    uint8_t *msg_ptr;

    // Append register address to front of data packet
    msg[0] = addr;
    for (int i = 0; i < nbytes; i++) {
        msg[i + 1] = buf[i];
    }
    //printf("EEPROM write page=%d addr=%d nbytes=%d\r\n",page,addr,nbytes);
    //for(i=0;i<(nbytes+1);i++) {
    //    //printf("[%x](%c)",msg[i],msg[i]);
    //    printf("%c",msg[i]);
    //}
    //printf("\r\n");
    gpio_put(EEPROM_WR_PIN, 0); // WR protection OFF
    sleep_ms(20);
    msg_ptr = msg;
    ret=i2c_write_blocking(i2c, device_address, msg_ptr, (nbytes + 1), false);
    sleep_ms(20); // EEPROM write takes some time
    gpio_put(EEPROM_WR_PIN, 1); // WR protection ON
    return;
}
void eeprom_write(i2c_inst_t *i2c, const uint8_t dev_addr,
                  const uint8_t page, const uint8_t addr,
                  uint8_t *buf, const uint8_t nbytes)
{
    uint8_t nextaddr = addr;
    uint8_t remain=8;
    uint8_t len=nbytes;
    uint8_t idx = 0;

    //printf("EEPROM write addr=%d page=%d len=%d\r\n",addr,page,nbytes);

    if(nbytes <= 8) {
        eeprom_write_small(i2c, dev_addr, page, addr, buf, nbytes);
    } else {
        // For the 24C16 EEPROM the max buf size to write at once is 8 (or 16?)
        while(len > 0) {
            //printf("EEPROM write nextaddr=%d idx=%d remain=%d\r\n",nextaddr,idx,remain);
            eeprom_write_small(i2c, dev_addr, page, nextaddr, &buf[idx], remain);
            len -= remain;
            nextaddr += remain;
            idx += remain;
            if(len < 8) remain = len;
        }
    }
    return;
}
int eeprom_read_small(i2c_inst_t *i2c, const uint8_t dev_addr,
                const uint8_t page, const uint8_t addr,
                uint8_t *buf, const uint8_t nbytes)
{
    uint8_t device_address = dev_addr | page;
    int num_bytes_read = 0;
    int i;

    i2c_write_blocking(i2c, device_address, &addr, 1, true);
    //i2c_write_blocking(i2c, device_address, &addr, 1, false);
    num_bytes_read = i2c_read_blocking(i2c, device_address, buf, nbytes, false);
    //printf("EEPROM read page=%d addr=%d:\r\n",page,addr);
    //for(i=0;i<num_bytes_read;i++) {
    //    //printf("[%x](%c)",buf[i],buf[i]);
    //    printf("%c[%x]",buf[i],buf[i]);
    //}
    //printf("\r\n");

    return num_bytes_read;
}
int eeprom_read(i2c_inst_t *i2c, const uint8_t dev_addr,
                const uint8_t page, const uint8_t addr,
                uint8_t *buf, const uint8_t nbytes)
{
    uint8_t nextaddr = addr;
    uint8_t remain=8;
    uint8_t len=nbytes;
    uint8_t idx = 0;
    int i;
    printf("EEPROM read page=%d addr=%d len=%d\r\n",page,addr,nbytes);
    if(nbytes <= 8) {
        eeprom_read_small(i2c, dev_addr, page, addr, buf, nbytes);
    } else {
        // For the 24C16 EEPROM the max buf size to read at once is 8 (or 16?)
        while(len > 0) {
            eeprom_read_small(i2c, dev_addr, page, nextaddr, &buf[idx], remain);
            len -= remain;
            nextaddr += remain;
            idx += remain;
            if(len < 8) remain = len;
        }
    }
    for(i=0;i<nbytes;i++) {
        printf("%c[%x]",buf[i],buf[i]);
    }
    printf("\r\n");
    return nbytes;
}



/*
 * increment or decrement char to next display-able char in ASCII table
 * non display-able chars turn into spaces
 */
char edit_char(char c, char edit)
{
    char new_c = c;
    if(c < 0x20 || c > 0x7E) {
        new_c = 0x20; // char just added behind
    } else {
        new_c += edit; // increment or decrement
        if(new_c == 0x1F) { new_c=0; return new_c; } // delete char
        if(new_c > 0x7E) new_c=0x20;
        if(new_c < 0x1F) new_c=0x7E;
    }
    return new_c;
}

/*
 * remove non-display-able chars
 * remove trailing spaces
 */
void fix_string(char *str)
{
    int i;
    for(i=0;i<MAX_STR_LEN;i++) {
        if(str[i] > 0x7E || str[i] < 0x20) str[i] = 0;
    }
    i=MAX_STR_LEN-1;
    while(i>0) {
        if(str[i] == 0x20 || str[i] == 0) str[i] = 0;
        else break;
        i--;
    }
}

char * find_replace(char *ptr, char find, char replace, int maxlen)
{
    char *p = ptr;
    int idx=maxlen;
    while(1) {
        if(*p == find) {
            *p = replace;
            break;
        } else {
            p++;
            idx--;
            if(idx==0) break;
        }
    }
    return p;
}

    char *intro_logo = (char*)INTRO_LOGO;
    char *version_text = (char*)GATEWAY_VERSION;
    char *main_menu_title =  (char*)"------- menu ------- ";
    char main_menu[4][MAX_STR_LEN] = {"  Display sensors    ",
                                      "  Setup Wifi         ",
                                      "  Set Tomofarm name  ",
                                      "  Set Thingspeak APIs"};
    char *sensors_title = (char*)   "------- sensors -----";
    char sensor_menu[4][MAX_STR_LEN]={"  Sensor 0           ",
                                      "  Sensor 1           ",
                                      "  Sensor 2           ",
                                      "  Sensor 3           "};
    char *wifi_ssid_title = (char*) "> - Setup WiFi SSID -";
    char *wifi_passwd_title=(char*) ">  Setup WiFi Passwd ";
    char *farm_name_title =(char*)  "> -- Set farm name --";
    char *url_title =(char*)        "> -- Set URL --      ";
    char *api_title =(char*)        "  ---Set APIs -------";
    char apis[4][MAX_STR_LEN] =     { SENSOR_API0,
                                      SENSOR_API1,
                                      SENSOR_API2,
                                      SENSOR_API3};
    char sensor_nr_menu[4][MAX_STR_LEN]={"  0:",
                                         "  1:",
                                         "  2:",
                                         "  3:"};

    int menu_selected_idx = 0;
    int selected_menu = 0;
    bool display_needs_refresh=true;
    char farm_name[MAX_STR_LEN]={FARM_NAME};
    char wifi_ssid[MAX_STR_LEN]={WIFI_SSID};
    char wifi_passwd[MAX_STR_LEN]={WIFI_PASSWORD};
    char thing_url[MAX_STR_LEN]={API_SERVER};
    char edit_position[MAX_STR_LEN]={"                     "};
    char sensor__field1[4][MAX_STR_LEN]= {"Room Temp  ? C       "};
    char sensor__field2[4][MAX_STR_LEN]= {"Room Hum   ? %       "};
    char sensor__field3[4][MAX_STR_LEN]= {"Room Light ? %       "};
    int  edit_position_idx = 0;

    ssd1306_t disp;

    void animation_init(void)
    {
        disp.external_vcc=false;
        ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
        ssd1306_clear(&disp);


        ssd1306_clear(&disp);
        //                         x, y,  scale, text
        ssd1306_draw_string(&disp, 8, 0, 2,     "Hello");
        ssd1306_draw_string(&disp, 8, 16, 2,    farm_name);
        ssd1306_draw_string(&disp, 0, 36, 1,     intro_logo);
        ssd1306_draw_string(&disp, 0, 48, 1,    "version:");
        ssd1306_draw_string(&disp, 70, 48, 1,    version_text);
        ssd1306_show(&disp);
        sleep_ms(2000);
    }
    void animation_wifi(int status)
    {
        disp.external_vcc=false;
        ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
        ssd1306_clear(&disp);


        ssd1306_clear(&disp);
        //                         x, y,  scale, text
        ssd1306_draw_string(&disp, 8, 0, 2,     "Hello");
        ssd1306_draw_string(&disp, 8, 16, 2,    farm_name);
        if(status == 0) {
            ssd1306_draw_string(&disp, 0, 36, 1,    "Connecting to WiFi:");
            ssd1306_draw_string(&disp, 0, 48, 1,    wifi_ssid);
        } else if (status == 1){
            ssd1306_draw_string(&disp, 0, 36, 1,    "Wifi connected: OK");
        } else {
            ssd1306_draw_string(&disp, 0, 36, 1,    "Wifi connected: FAIL");
        }
        ssd1306_show(&disp);
        sleep_ms(1000);
    }

    int sleep_ms_cond(int ms)
    {
        int remain = ms;
        while(remain > 0) {
             if(any_button_pressed()) {
                reset_all_buttons();
                return 1; // abort sleep at button press
            }
            sleep_ms(100);
            remain -= 100;
        }
        return 0;
    }
    void animation(void) {
    //for(;;) {
        if(display_needs_refresh) {
            int i;
            display_needs_refresh=false;
            ssd1306_clear(&disp);
            if(selected_menu == MENU_MAIN) {
                ssd1306_draw_string(&disp, 0, 0, 1, main_menu_title);
                for(i=0;i<4;i++) {
                    main_menu[i][0] = ' ';
                }
                main_menu[menu_selected_idx][0] = '>';
                ssd1306_draw_string(&disp, 0, 12, 1,main_menu[0]);
                ssd1306_draw_string(&disp, 0, 24, 1,main_menu[1]);
                ssd1306_draw_string(&disp, 0, 36, 1,main_menu[2]);
                ssd1306_draw_string(&disp, 0, 48, 1,main_menu[3]);
            } else if(selected_menu == MENU_SENSOR) {
                ssd1306_draw_string(&disp, 0, 0, 1, sensors_title);
                for(i=0;i<4;i++) {
                    sensor_menu[i][0] = ' ';
                }
                sensor_menu[menu_selected_idx][0] = '>';
                ssd1306_draw_string(&disp, 0, 12, 1,sensor_menu[0]);
                ssd1306_draw_string(&disp, 0, 24, 1,sensor_menu[1]);
                ssd1306_draw_string(&disp, 0, 36, 1,sensor_menu[2]);
                ssd1306_draw_string(&disp, 0, 48, 1,sensor_menu[3]);
            } else if(selected_menu == MENU_SENSOR_0) {
                ssd1306_draw_string(&disp, 0, 0, 1, "sensor0 data");
                ssd1306_draw_string(&disp, 0, 24, 1,sensor__field1[0]);
                ssd1306_draw_string(&disp, 0, 36, 1,sensor__field2[0]);
                ssd1306_draw_string(&disp, 0, 48, 1,sensor__field3[0]);
            } else if(selected_menu == MENU_SENSOR_1) {
                ssd1306_draw_string(&disp, 0, 0, 1, "sensor1 data");
                ssd1306_draw_string(&disp, 0, 24, 1,sensor__field1[1]);
                ssd1306_draw_string(&disp, 0, 36, 1,sensor__field2[1]);
                ssd1306_draw_string(&disp, 0, 48, 1,sensor__field3[1]);
            } else if(selected_menu == MENU_SENSOR_2) {
                ssd1306_draw_string(&disp, 0, 0, 1, "sensor2 data");
                ssd1306_draw_string(&disp, 0, 24, 1,sensor__field1[2]);
                ssd1306_draw_string(&disp, 0, 36, 1,sensor__field2[2]);
                ssd1306_draw_string(&disp, 0, 48, 1,sensor__field3[2]);
            } else if(selected_menu == MENU_SENSOR_3) {
                ssd1306_draw_string(&disp, 0, 0, 1, "sensor3 data");
                ssd1306_draw_string(&disp, 0, 24, 1,sensor__field1[3]);
                ssd1306_draw_string(&disp, 0, 36, 1,sensor__field2[3]);
                ssd1306_draw_string(&disp, 0, 48, 1,sensor__field3[3]);
            } else if(selected_menu == MENU_WIFI) {
                strcpy(edit_position, "                     ");
                edit_position[edit_position_idx] = '^';
                ssd1306_draw_string(&disp, 0, 0, 1, wifi_ssid_title);
                ssd1306_draw_string(&disp, 0, 24, 1,wifi_ssid);
                ssd1306_draw_string(&disp, 0, 36, 1,edit_position);
            } else if(selected_menu == MENU_WIFI_PASSWD) {
                strcpy(edit_position, "                     ");
                edit_position[edit_position_idx] = '^';
                ssd1306_draw_string(&disp, 0, 0, 1, wifi_passwd_title);
                ssd1306_draw_string(&disp, 0, 24, 1,wifi_passwd);
                ssd1306_draw_string(&disp, 0, 36, 1,edit_position);
            } else if(selected_menu == MENU_URL) {
                strcpy(edit_position, "                     ");
                edit_position[edit_position_idx] = '^';
                ssd1306_draw_string(&disp, 0, 0, 1, url_title);
                ssd1306_draw_string(&disp, 0, 24, 1,thing_url);
                ssd1306_draw_string(&disp, 0, 36, 1,edit_position);
            } else if(selected_menu == MENU_FARM) {
                strcpy(edit_position, "                     ");
                edit_position[edit_position_idx] = '^';
                ssd1306_draw_string(&disp, 0, 0, 1, farm_name_title);
                ssd1306_draw_string(&disp, 0, 24, 1,farm_name);
                ssd1306_draw_string(&disp, 0, 36, 1,edit_position);
            } else if(selected_menu == MENU_API) {
                ssd1306_draw_string(&disp, 0, 0, 1, api_title);
                for(i=0;i<4;i++) {
                    sensor_nr_menu[i][0] = ' ';
                }
                sensor_nr_menu[menu_selected_idx][0] = '>';
                ssd1306_draw_string(&disp, 0, 12, 1,sensor_nr_menu[0]);
                ssd1306_draw_string(&disp, 0, 24, 1,sensor_nr_menu[1]);
                ssd1306_draw_string(&disp, 0, 36, 1,sensor_nr_menu[2]);
                ssd1306_draw_string(&disp, 0, 48, 1,sensor_nr_menu[3]);
                ssd1306_draw_string(&disp, 28, 12, 1,apis[0]);
                ssd1306_draw_string(&disp, 28, 24, 1,apis[1]);
                ssd1306_draw_string(&disp, 28, 36, 1,apis[2]);
                ssd1306_draw_string(&disp, 28, 48, 1,apis[3]);
            } else if(selected_menu == MENU_API_0) {
                strcpy(edit_position, "                     ");
                edit_position[edit_position_idx] = '^';
                ssd1306_draw_string(&disp, 0, 0, 1, "Set API sensor 0");
                ssd1306_draw_string(&disp, 0, 24, 1,apis[0]);
                ssd1306_draw_string(&disp, 0, 36, 1,edit_position);
            } else if(selected_menu == MENU_API_1) {
                strcpy(edit_position, "                     ");
                edit_position[edit_position_idx] = '^';
                ssd1306_draw_string(&disp, 0, 0, 1, "Set API sensor 1");
                ssd1306_draw_string(&disp, 0, 24, 1,apis[1]);
                ssd1306_draw_string(&disp, 0, 36, 1,edit_position);
            } else if(selected_menu == MENU_API_2) {
                strcpy(edit_position, "                     ");
                edit_position[edit_position_idx] = '^';
                ssd1306_draw_string(&disp, 0, 0, 1, "Set API sensor 2");
                ssd1306_draw_string(&disp, 0, 24, 1,apis[2]);
                ssd1306_draw_string(&disp, 0, 36, 1,edit_position);
            } else if(selected_menu == MENU_API_3) {
                strcpy(edit_position, "                     ");
                edit_position[edit_position_idx] = '^';
                ssd1306_draw_string(&disp, 0, 0, 1, "Set API sensor 3");
                ssd1306_draw_string(&disp, 0, 24, 1,apis[3]);
                ssd1306_draw_string(&disp, 0, 36, 1,edit_position);
            }
            ssd1306_show(&disp);
        }
        if(selected_menu == MENU_MAIN) {
            if(button[BUTTON_ENTER]) {
                button[BUTTON_ENTER]=false;
                selected_menu = menu_selected_idx+10;
                display_needs_refresh=true;
            } else if(button[BUTTON_UP]) {
                button[BUTTON_UP]=false;
                menu_selected_idx--; if(menu_selected_idx<0) menu_selected_idx=3;
                display_needs_refresh=true;
            } else if(button[BUTTON_DOWN]) {
                button[BUTTON_DOWN]=false;
                menu_selected_idx++; if(menu_selected_idx>=4) menu_selected_idx=0;
                display_needs_refresh=true;
            }
        } else if(selected_menu == MENU_SENSOR) {
            if(button[BUTTON_ENTER]) {
                button[BUTTON_ENTER]=false;
                selected_menu = menu_selected_idx+MENU_SENSOR_0;
                menu_selected_idx=0;
                display_needs_refresh=true;
            } else if(button[BUTTON_UP]) {
                button[BUTTON_UP]=false;
                menu_selected_idx--; if(menu_selected_idx<0) menu_selected_idx=3;
                display_needs_refresh=true;
            } else if(button[BUTTON_DOWN]) {
                button[BUTTON_DOWN]=false;
                menu_selected_idx++; if(menu_selected_idx>=4) menu_selected_idx=0;
                display_needs_refresh=true;
            }
        } else if(selected_menu == MENU_SENSOR_0 ||
                  selected_menu == MENU_SENSOR_1 ||
                  selected_menu == MENU_SENSOR_2 ||
                  selected_menu == MENU_SENSOR_3) {
            if(button[BUTTON_ENTER]) {
                button[BUTTON_ENTER]=false;
                selected_menu = MENU_MAIN;
                menu_selected_idx = 0;
                display_needs_refresh=true;
            } else if(button[BUTTON_UP]) {
                button[BUTTON_UP]=false;
                selected_menu--; if(selected_menu<MENU_SENSOR_0) selected_menu=MENU_SENSOR_3;
                display_needs_refresh=true;
            } else if(button[BUTTON_DOWN]) {
                button[BUTTON_DOWN]=false;
                selected_menu++; if(selected_menu>MENU_SENSOR_3) selected_menu=MENU_SENSOR_0;
                display_needs_refresh=true;
            }
        } else if(selected_menu == MENU_WIFI) {
            if(button[BUTTON_ENTER]) {
                uint8_t *ptr;
                button[BUTTON_ENTER]=false;
                selected_menu = MENU_WIFI_PASSWD;
                display_needs_refresh=true;
                fix_string(wifi_ssid);
                printf("New WiFi SSID=[%s]\n",wifi_ssid);
                ptr = (uint8_t *)wifi_ssid;
                eeprom_write(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_WifiSSID,
                             ptr,strlen(wifi_ssid)+1);
            } else if(button[BUTTON_RIGHT]) {
                button[BUTTON_RIGHT]=false;
                edit_position_idx++; if(edit_position_idx>=20) edit_position_idx=0;
                display_needs_refresh=true;
            } else if(button[BUTTON_LEFT]) {
                button[BUTTON_LEFT]=false;
                edit_position_idx--; if(edit_position_idx<0) edit_position_idx=20;
                display_needs_refresh=true;
            } else if(button[BUTTON_UP]) {
                button[BUTTON_UP]=false;
                wifi_ssid[edit_position_idx] = edit_char(wifi_ssid[edit_position_idx], +1);
                display_needs_refresh=true;
            } else if(button[BUTTON_DOWN]) {
                button[BUTTON_DOWN]=false;
                wifi_ssid[edit_position_idx] = edit_char(wifi_ssid[edit_position_idx], -1);
                display_needs_refresh=true;
            }
        } else if(selected_menu == MENU_WIFI_PASSWD) {
            if(button[BUTTON_ENTER]) {
                uint8_t *ptr;
                button[BUTTON_ENTER]=false;
                selected_menu = MENU_URL;
                display_needs_refresh=true;
                fix_string(wifi_passwd);
                printf("New WiFi Password=[%s]\n",wifi_passwd);
                ptr = (uint8_t *)wifi_passwd;
                eeprom_write(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_WifiPass,
                             ptr,strlen(wifi_passwd)+1);
            } else if(button[BUTTON_RIGHT]) {
                button[BUTTON_RIGHT]=false;
                edit_position_idx++; if(edit_position_idx>=20) edit_position_idx=0;
                display_needs_refresh=true;
            } else if(button[BUTTON_LEFT]) {
                button[BUTTON_LEFT]=false;
                edit_position_idx--; if(edit_position_idx<0) edit_position_idx=20;
                display_needs_refresh=true;
            } else if(button[BUTTON_UP]) {
                button[BUTTON_UP]=false;
                wifi_passwd[edit_position_idx] = edit_char(wifi_passwd[edit_position_idx], +1);
                display_needs_refresh=true;
            } else if(button[BUTTON_DOWN]) {
                button[BUTTON_DOWN]=false;
                wifi_passwd[edit_position_idx] = edit_char(wifi_passwd[edit_position_idx], -1);
                display_needs_refresh=true;
            }
        } else if(selected_menu == MENU_URL) {
            if(button[BUTTON_ENTER]) {
                uint8_t *ptr;
                button[BUTTON_ENTER]=false;
                selected_menu = MENU_MAIN;
                display_needs_refresh=true;
                fix_string(thing_url);
                printf("New URL=[%s]\n",thing_url);
                ptr = (uint8_t *)thing_url;
                eeprom_write(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_URL,
                             ptr,strlen(thing_url)+1);
            } else if(button[BUTTON_RIGHT]) {
                button[BUTTON_RIGHT]=false;
                edit_position_idx++; if(edit_position_idx>=20) edit_position_idx=0;
                display_needs_refresh=true;
            } else if(button[BUTTON_LEFT]) {
                button[BUTTON_LEFT]=false;
                edit_position_idx--; if(edit_position_idx<0) edit_position_idx=20;
                display_needs_refresh=true;
            } else if(button[BUTTON_UP]) {
                button[BUTTON_UP]=false;
                thing_url[edit_position_idx] = edit_char(thing_url[edit_position_idx], +1);
                display_needs_refresh=true;
            } else if(button[BUTTON_DOWN]) {
                button[BUTTON_DOWN]=false;
                thing_url[edit_position_idx] = edit_char(thing_url[edit_position_idx], -1);
                display_needs_refresh=true;
            }
        } else if(selected_menu == MENU_FARM) {
            if(button[BUTTON_ENTER]) {
                uint8_t *ptr;
                button[BUTTON_ENTER]=false;
                selected_menu = MENU_MAIN;
                display_needs_refresh=true;
                fix_string(farm_name);
                printf("New Farm name=[%s]\n",farm_name);
                ptr = (uint8_t *)farm_name;
                eeprom_write(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_FARM,
                             ptr,strlen(farm_name)+1);
            } else if(button[BUTTON_RIGHT]) {
                button[BUTTON_RIGHT]=false;
                edit_position_idx++; if(edit_position_idx>=20) edit_position_idx=0;
                display_needs_refresh=true;
            } else if(button[BUTTON_LEFT]) {
                button[BUTTON_LEFT]=false;
                edit_position_idx--; if(edit_position_idx<0) edit_position_idx=20;
                display_needs_refresh=true;
            } else if(button[BUTTON_UP]) {
                button[BUTTON_UP]=false;
                farm_name[edit_position_idx] = edit_char(farm_name[edit_position_idx], +1);
                display_needs_refresh=true;
            } else if(button[BUTTON_DOWN]) {
                button[BUTTON_DOWN]=false;
                farm_name[edit_position_idx] = edit_char(farm_name[edit_position_idx], -1);
                display_needs_refresh=true;
            }
        } else if(selected_menu == MENU_API) {
            if(button[BUTTON_ENTER]) {
                button[BUTTON_ENTER]=false;
                selected_menu = menu_selected_idx+MENU_API_1;
                menu_selected_idx=0;
                display_needs_refresh=true;
            } else if(button[BUTTON_UP]) {
                button[BUTTON_UP]=false;
                menu_selected_idx--; if(menu_selected_idx<0) menu_selected_idx=3;
                display_needs_refresh=true;
            } else if(button[BUTTON_DOWN]) {
                button[BUTTON_DOWN]=false;
                menu_selected_idx++; if(menu_selected_idx>=4) menu_selected_idx=0;
                display_needs_refresh=true;
            }
        } else if(selected_menu == MENU_API_0 ||
                  selected_menu == MENU_API_1 ||
                  selected_menu == MENU_API_2 ||
                  selected_menu == MENU_API_3) {
            if(button[BUTTON_ENTER]) {
                uint8_t *ptr;
                button[BUTTON_ENTER]=false;
                display_needs_refresh=true;
                fix_string(apis[selected_menu-MENU_API_0]);
                printf("New API[%d]=[%s]\n",selected_menu-MENU_API_0,apis[selected_menu-MENU_API_0]);
                ptr = (uint8_t *)(apis[selected_menu-MENU_API_0]);
                eeprom_write(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_API0+(32*(selected_menu-MENU_API_0)),
                             ptr,strlen(apis[selected_menu-MENU_API_0])+1);
                menu_selected_idx = 0;
                selected_menu = MENU_MAIN;
            } else if(button[BUTTON_RIGHT]) {
                button[BUTTON_RIGHT]=false;
                edit_position_idx++; if(edit_position_idx>=20) edit_position_idx=0;
                display_needs_refresh=true;
            } else if(button[BUTTON_LEFT]) {
                button[BUTTON_LEFT]=false;
                edit_position_idx--; if(edit_position_idx<0) edit_position_idx=20;
                display_needs_refresh=true;
            } else if(button[BUTTON_UP]) {
                button[BUTTON_UP]=false;
                apis[selected_menu-MENU_API_1][edit_position_idx] = edit_char(apis[selected_menu-MENU_API_0][edit_position_idx], +1);
                display_needs_refresh=true;
            } else if(button[BUTTON_DOWN]) {
                button[BUTTON_DOWN]=false;
                apis[selected_menu-MENU_API_1][edit_position_idx] = edit_char(apis[selected_menu-MENU_API_0][edit_position_idx], -1);
                display_needs_refresh=true;
            }
        }
    //}
}

typedef struct TCP_CLIENT_T_ {
    struct tcp_pcb *tcp_pcb;
    ip_addr_t remote_addr;
    uint8_t buffer[BUF_SIZE];
    int buffer_len;
    int sent_len;
    bool complete;
    int run_count;
    bool connected;
    bool dns_request_sent;
    bool dns_request_ready;
} TCP_CLIENT_T;

void pico_do_reboot(void)
{
    printf("Reboot now!\n");
    watchdog_enable(100, 1);
    while(1);
}

// Call back with a DNS result
static void dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (ipaddr) {
        state->remote_addr = *ipaddr;
        printf("server address %s\n", ip4addr_ntoa(ipaddr));
        //ntp_request(state);
    } else {
        printf("dns request failed\n");
        //ntp_result(state, -1, NULL);
    }
    state->dns_request_ready=true;
}

static err_t tcp_client_close(void *arg) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    err_t err = ERR_OK;
    if (state->tcp_pcb != NULL) {
        tcp_arg(state->tcp_pcb, NULL);
        tcp_poll(state->tcp_pcb, NULL, 0);
        tcp_sent(state->tcp_pcb, NULL);
        tcp_recv(state->tcp_pcb, NULL);
        tcp_err(state->tcp_pcb, NULL);
        err = tcp_close(state->tcp_pcb);
        if (err != ERR_OK) {
            DEBUG_printf("close failed %d, calling abort\n", err);
            tcp_abort(state->tcp_pcb);
            err = ERR_ABRT;
        }
        state->tcp_pcb = NULL;
    }
    return err;
}

// Called with results of operation
static err_t tcp_result(void *arg, int status) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (status == 0) {
        DEBUG_printf("success\n");
    } else {
        //DEBUG_printf("test failed %d\n", status);
        DEBUG_printf("ended with %d\n", status);
    }
    state->complete = true;
    return tcp_client_close(arg);
}

static err_t tcp_client_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    DEBUG_printf("tcp_client_sent %u\n", len);
    state->sent_len += len;

    if (state->sent_len >= BUF_SIZE) {

        state->run_count++;
        if (state->run_count >= TEST_ITERATIONS) {
            tcp_result(arg, 0);
            return ERR_OK;
        }

        // We should receive a new buffer from the server
        state->buffer_len = 0;
        state->sent_len = 0;
        DEBUG_printf("Waiting for buffer from server\n");
    }

    return ERR_OK;
}

static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (err != ERR_OK) {
        printf("connect failed %d\n", err);
        return tcp_result(arg, err);
    }
    state->connected = true;
    return ERR_OK;
}

static err_t tcp_client_poll(void *arg, struct tcp_pcb *tpcb) {
    DEBUG_printf("tcp_client_poll\n");
    return tcp_result(arg, -1); // no response is an error?
}

static void tcp_client_err(void *arg, err_t err) {
    if (err != ERR_ABRT) {
        DEBUG_printf("tcp_client_err %d\n", err);
        tcp_result(arg, err);
    }
}

err_t tcp_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (!p) {
        return tcp_result(arg, -1);
    }
    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
    cyw43_arch_lwip_check();
    if (p->tot_len > 0) {
        DEBUG_printf("recv %d err %d\n", p->tot_len, err);
        for (struct pbuf *q = p; q != NULL; q = q->next) {
            DUMP_BYTES(q->payload, q->len);
        }
        // Receive the buffer
        const uint16_t buffer_left = BUF_SIZE - state->buffer_len;
        state->buffer_len += pbuf_copy_partial(p, state->buffer + state->buffer_len,
                                               p->tot_len > buffer_left ? buffer_left : p->tot_len, 0);
        tcp_recved(tpcb, p->tot_len);
    }
    pbuf_free(p);

    return ERR_OK;
}

static bool tcp_client_open(void *arg) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;

    state->dns_request_ready=false;
    cyw43_arch_lwip_begin();
    int dns_err = dns_gethostbyname(thing_url, &state->remote_addr, dns_found, state);
    cyw43_arch_lwip_end();
    
    state->dns_request_sent = true;
    while(1) {
        watchdog_update();
        if (dns_err == ERR_OK) {
        break;
        } else if (dns_err != ERR_INPROGRESS) { // ERR_INPROGRESS means expect a callback
            printf("dns request failed\n");
        return ERR_ABRT;
        } 
    if(state->dns_request_ready == true) break;
        sleep_ms(5000);
    }
    watchdog_update();

    DEBUG_printf("Connecting to %s port %u\n", ip4addr_ntoa(&state->remote_addr), TCP_PORT);
    state->tcp_pcb = tcp_new_ip_type(IP_GET_TYPE(&state->remote_addr));
    if (!state->tcp_pcb) {
        DEBUG_printf("failed to create pcb\n");
        return false;
    }

    tcp_arg(state->tcp_pcb, state);
    tcp_poll(state->tcp_pcb, tcp_client_poll, POLL_TIME_S * 2);
    tcp_sent(state->tcp_pcb, tcp_client_sent);
    tcp_recv(state->tcp_pcb, tcp_client_recv);
    tcp_err(state->tcp_pcb, tcp_client_err);

    state->buffer_len = 0;

    // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure correct locking.
    // You can omit them if you are in a callback from lwIP. Note that when using pico_cyw_arch_poll
    // these calls are a no-op and can be omitted, but it is a good practice to use them in
    // case you switch the cyw43_arch type later.
    cyw43_arch_lwip_begin();
    err_t err = tcp_connect(state->tcp_pcb, &state->remote_addr, TCP_PORT, tcp_client_connected);
    cyw43_arch_lwip_end();

    return err == ERR_OK;
}

// Perform initialisation
static TCP_CLIENT_T* tcp_client_init(void) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T *)calloc(1, sizeof(TCP_CLIENT_T));
    if (!state) {
        DEBUG_printf("failed to allocate state\n");
        return NULL;
    }
    //ip4addr_aton(TEST_API_SERVER_IP, &state->remote_addr);
    return state;
}

//void run_tcp_client(float temp, float hum) {
void run_tcp_client(char *from_lora) {
    err_t err_write;
    TCP_CLIENT_T *state = tcp_client_init();
    if (!state) {
        return;
    }
    if (!tcp_client_open(state)) {
        tcp_result(state, -1);
        return;
    }
    cyw43_arch_lwip_check();


    watchdog_update();
    sprintf((char *)state->buffer,"GET /update?api_key%s HTTP/1.1\r\nHost: %s\r\n\r\n",from_lora,thing_url);
    DEBUG_printf("Sending: %s\n",(char *)state->buffer);
    state->buffer_len = strlen((char *)state->buffer);
    err_write = tcp_write(state->tcp_pcb, state->buffer, state->buffer_len, TCP_WRITE_FLAG_COPY);
    if (err_write != ERR_OK) {
        DEBUG_printf("Failed to write data %d\n", err_write);
        tcp_result(state, -1);
        pico_do_reboot(); // let watchdog handle this
        return;
    }
    watchdog_update();
    sleep_ms(5000);
    watchdog_update();
    /*
    while(!state->complete) {
        // the following #ifdef is only here so this same example can be used in multiple modes;
        // you do not need it in your code
#if PICO_CYW43_ARCH_POLL
        // if you are using pico_cyw43_arch_poll, then you must poll periodically from your
        // main loop (not from a timer) to check for WiFi driver or lwIP work that needs to be done.
        cyw43_arch_poll();
        sleep_ms(1);
#else
        // if you are not using pico_cyw43_arch_poll, then WiFI driver and lwIP work
        // is done via interrupt in the background. This sleep is just an example of some (blocking)
        // work you might be doing.
        sleep_ms(1000);
#endif
    }
    */
    free(state);
}


int main() {
    uint32_t pico_uptime=0;
    absolute_time_t interval_time;

    memset((void *)button,0,sizeof(button));
    memset((void *)button_pressed,0,sizeof(button_pressed));
    memset((void *)button_cnt,0,sizeof(button_cnt));
    any_button_pressed_delay=0;

    stdio_init_all();
    adc_init();
    //adc_gpio_init(26); // EC
    //adc_select_input(0);
    adc_gpio_init(27); // pH or light sensor
    adc_select_input(1);
    setup_gpios();
    //const float conversion_factor = 3.3f / (1 << 12); // volt
    const float conversion_factor = 100.0 / 0x9ff; // %
    dht_t dht;
    float humidity;
    float temperature_c;
    float light_c;
    uint16_t adc_result;
    char buf[100];
    char bufcpy[100];
    char *bufptr;
    char *bufptrn;

    sleep_ms(3000);

    if (watchdog_caused_reboot()) {
        printf("Rebooted by watchdog\r\n");
    } else {
        printf("Clean boot\r\n");
    }
    printf("Gateway [%s] startup\r\n",GATEWAY_VERSION);
    //printf("sizeof(int)=%d\r\n",sizeof(int)); // it's 4

    // EEPROM read
    // read the first byte from offset 0; if it is 0xFF, then the EEPROM
    // is new -> write initial contents and reboot. Otherwise read all
    eeprom_read(i2c0, EEPROM_DEV_ADDR, 0, 0, (uint8_t *)buf, 1);
    if(buf[0] == 0x01 || buf[0] == 0xff) {
        printf("Set initial EEPROM\r\n");
        buf[0] = 0x02;

        eeprom_write(i2c0, EEPROM_DEV_ADDR, 0, 0, (uint8_t *)buf, 1);

        eeprom_write(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_FARM,
                     (uint8_t *)EEPROM_CLR, 32);
        eeprom_write(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_WifiSSID,
                     (uint8_t *)EEPROM_CLR, 32);
        eeprom_write(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_WifiPass,
                     (uint8_t *)EEPROM_CLR, 32);
        eeprom_write(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_URL,
                     (uint8_t *)EEPROM_CLR, 32);
        eeprom_write(i2c0, EEPROM_DEV_ADDR, 1, EEPROM_OFFS_API0,
                     (uint8_t *)EEPROM_CLR, 32);
        eeprom_write(i2c0, EEPROM_DEV_ADDR, 1, EEPROM_OFFS_API1,
                     (uint8_t *)EEPROM_CLR, 32);
        eeprom_write(i2c0, EEPROM_DEV_ADDR, 1, EEPROM_OFFS_API2,
                     (uint8_t *)EEPROM_CLR, 32);
        eeprom_write(i2c0, EEPROM_DEV_ADDR, 1, EEPROM_OFFS_API3,
                     (uint8_t *)EEPROM_CLR, 32);

        eeprom_write(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_FARM,
                     (uint8_t *)FARM_NAME,strlen(FARM_NAME));
        eeprom_write(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_WifiSSID,
                     (uint8_t *)WIFI_SSID,strlen(WIFI_SSID));
        eeprom_write(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_WifiPass,
                     (uint8_t *)WIFI_PASSWORD,strlen(WIFI_PASSWORD));
        eeprom_write(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_URL,
                     (uint8_t *)API_SERVER,strlen(API_SERVER));
        eeprom_write(i2c0, EEPROM_DEV_ADDR, 1, EEPROM_OFFS_API0,
                     (uint8_t *)SENSOR_API0,strlen(SENSOR_API0));
        eeprom_write(i2c0, EEPROM_DEV_ADDR, 1, EEPROM_OFFS_API1,
                     (uint8_t *)SENSOR_API1,strlen(SENSOR_API1));
        eeprom_write(i2c0, EEPROM_DEV_ADDR, 1, EEPROM_OFFS_API2,
                     (uint8_t *)SENSOR_API2,strlen(SENSOR_API2));
        eeprom_write(i2c0, EEPROM_DEV_ADDR, 1, EEPROM_OFFS_API3,
                     (uint8_t *)SENSOR_API3,strlen(SENSOR_API3));

        watchdog_enable(0x7fff, 1);
        pico_do_reboot();
    } else {
        uint8_t *buf_ptr;
        buf_ptr=(uint8_t *)farm_name;
        eeprom_read(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_FARM,
                     buf_ptr,MAX_STR_LEN);
        buf_ptr=(uint8_t *)wifi_ssid;
        eeprom_read(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_WifiSSID,
                     buf_ptr,MAX_STR_LEN);
        buf_ptr=(uint8_t *)wifi_passwd;
        eeprom_read(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_WifiPass,
                     buf_ptr,MAX_STR_LEN);
        buf_ptr=(uint8_t *)thing_url;
        eeprom_read(i2c0, EEPROM_DEV_ADDR, 0, EEPROM_OFFS_URL,
                     buf_ptr,MAX_STR_LEN);
        buf_ptr=(uint8_t *)apis[0];
        eeprom_read(i2c0, EEPROM_DEV_ADDR, 1, EEPROM_OFFS_API0,
                     buf_ptr,MAX_STR_LEN);
        buf_ptr=(uint8_t *)apis[1];
        eeprom_read(i2c0, EEPROM_DEV_ADDR, 1, EEPROM_OFFS_API1,
                     buf_ptr,MAX_STR_LEN);
        buf_ptr=(uint8_t *)apis[2];
        eeprom_read(i2c0, EEPROM_DEV_ADDR, 1, EEPROM_OFFS_API2,
                     buf_ptr,MAX_STR_LEN);
        buf_ptr=(uint8_t *)apis[3];
        eeprom_read(i2c0, EEPROM_DEV_ADDR, 1, EEPROM_OFFS_API3,
                     buf_ptr,MAX_STR_LEN);
    }
    animation_init();

    // interrupt
    time_inter_enter = to_ms_since_boot(get_absolute_time());
    gpio_set_irq_enabled_with_callback(button_pin[BUTTON_ENTER], GPIO_IRQ_EDGE_FALL , true, &inter_enter);
    struct repeating_timer timer;
    add_repeating_timer_ms(-5, repeating_timer_callback, NULL, &timer);

    dht_init(&dht, DHT_MODEL, pio0, DATA_PIN, true /* pull_up */);


   while(1) {
      if (!LoRa.begin(868E6)) {
         printf("Starting LoRa failed!\n");
         sleep_ms(3000);
      } else break;
   }
   printf("LoRa Started\n");

    while(1) {
        if (cyw43_arch_init()) {
            DEBUG_printf("failed to initialise\n");
            sleep_ms(10000);
        } else break;
    }

    animation_wifi(0);

    cyw43_arch_enable_sta_mode();

    while(1) {
        printf("Connecting to WiFi...\r\n");
        printf("SSID=%s\r\n",wifi_ssid);
        printf("PASSWD=%s\r\n",wifi_passwd);
        if (cyw43_arch_wifi_connect_timeout_ms(wifi_ssid, wifi_passwd, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
            printf("failed to connect.\n");
            if(sleep_ms_cond(30000)) {
                animation_wifi(-1);
                break;
            }
        } else {
            printf("Connected.\n");
            animation_wifi(1);
            break;
        }
    }

    watchdog_enable(0x7fffff, 1); // 8 seconds (is max)
    interval_time = make_timeout_time_ms(SENSOR_INTERVAL_TIME);
    while(1) {
      animation();
      watchdog_update();
      if(any_button_pressed_delay == 0) {
          int packetSize = LoRa.parsePacket();
          if (packetSize) {
             // received a packet
             printf("Read packet len=%d:\n'",packetSize);
             if(packetSize > 100) { // bad packet
                 while(1) {
                    if (!LoRa.begin(868E6)) {
                       printf("Starting LoRa failed!\n");
                       sleep_ms(3000);
                    } else break;
                 }
                 printf("LoRa restarted due to illegal packet len\n");
             } else {
                 cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        
                 // read packet
                 bufptr=buf;
                 while (LoRa.available()) {
                    *bufptr++ = LoRa.read();
                 }
    
                 *bufptr++ = 0;
        
                 if(buf[0] == '=') {
                     // print RSSI of packet
                     printf("%s' with RSSI=%d\n",buf,LoRa.packetRssi());
                     int sensor_id=buf[1]&0xF;
                     if (sensor_id < 4 && sensor_id > 0) {
                         sprintf(bufcpy,"=%s%s",apis[sensor_id],&buf[2]);
                         watchdog_update();
                         run_tcp_client(bufcpy);
                         bufptrn=&buf[10];
                         bufptr=find_replace(bufptrn,'&','\0',20);
                         sprintf(sensor__field1[sensor_id],"Soil Temp  %s C",bufptrn);
                         bufptr+=8;
                         bufptrn=find_replace(bufptr,'&','\0',20);
                         sprintf(sensor__field2[sensor_id],"Soil Hum   %s %%",bufptr);
                         bufptrn+=8;
                         bufptr=find_replace(bufptrn,'&','\0',20);
                         sprintf(sensor__field3[sensor_id],"Soil pH    %s ",bufptrn);
                         display_needs_refresh=true;
                     } else {
                         printf("LoRa ignored bad sensor_id\n");
                     }
                 } else {
                     printf("LoRa ignored junk packet\n");
                 }
                 cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
             }
          }
      }
      watchdog_update();


      if(any_button_pressed_delay == 0) {
          if(absolute_time_diff_us(get_absolute_time(),interval_time) < 0) {
             cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
             interval_time = make_timeout_time_ms(SENSOR_INTERVAL_TIME);
             pico_uptime++;
             printf("Pico uptime is %d minutes\n",pico_uptime*2);
             // quick/dirty way to reset the WiFi connection
             if(pico_uptime >= REBOOT_AFTER_) {
                 cyw43_arch_deinit();
                 pico_do_reboot();
             }

             dht_start_measurement(&dht);
             dht_result_t result = dht_finish_measurement_blocking(&dht, &humidity, &temperature_c);
             if (result == DHT_RESULT_OK) {
                 printf("%.1f C (%.1f F), %.1f%% humidity\n", temperature_c, celsius_to_fahrenheit(temperature_c), humidity);
             } else if (result == DHT_RESULT_TIMEOUT) {
                 puts("DHT sensor not responding. Please check your wiring.");
             humidity=0.; temperature_c=0.;
             } else {
                 puts("Bad checksum");
             humidity=0.; temperature_c=0.;
             }
             adc_result = adc_read();
             light_c = adc_result * conversion_factor;
             //printf("Raw value: 0x%03x, voltage: %f V\n", 
             //        adc_result, light_c);
             // noticed that max is: Raw value: 0x9ff, voltage: 2.061694 V (lightsensor uses LM358 opamp which is not rail-to-rail)
             // https://wiki.seeedstudio.com/Grove-Light_Sensor/
             printf("Raw value: 0x%03x, light: %f %%\n", 
                     adc_result, light_c);


             sprintf(buf,"=" SENSOR_API0 "&field1=%.1f&field2=%.1f&field3=%.1f",
                     temperature_c,humidity,light_c); 
             sprintf(sensor__field1[0],"Room Temp  %.1f C",temperature_c);
             sprintf(sensor__field2[0],"Room Hum   %.1f %%",humidity);
             sprintf(sensor__field3[0],"Room Light %.1f %%",light_c);
             display_needs_refresh=true;
             watchdog_update();
             run_tcp_client(buf);
             cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
          }
      }
    }
    cyw43_arch_deinit();
    return 0;
}


