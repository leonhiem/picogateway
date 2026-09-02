cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(gateway C CXX ASM)

set(PICO_LORA_PATH ../LoRa-pi-pico-src)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_CXX_ENABLE_EXCEPTIONS 1)
pico_sdk_init()

add_subdirectory("../LoRa-pi-pico" LoRa_pico_lib)
add_subdirectory("../pico_dht" dht)
add_executable(gateway
        ssd1306.cpp
        gateway.cpp
        )
target_compile_definitions(gateway PRIVATE
        WIFI_SSID=\"${WIFI_SSID}\"
        WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
        )
target_include_directories(gateway PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts
	../LoRa-pi-pico/src
  ../
        )


target_link_libraries(gateway
        pico_cyw43_arch_lwip_threadsafe_background
        pico_stdlib
        )
target_link_libraries(gateway pico_stdlib LoRa_pico_lib hardware_spi hardware_interp LoRa_print hardware_adc hardware_gpio dht hardware_i2c)

pico_enable_stdio_uart(gateway 1)
pico_enable_stdio_usb(gateway 1)

pico_add_extra_outputs(gateway)

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        -Wno-maybe-uninitialized
)
