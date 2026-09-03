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

# picogateway_kernel: the reusable multitasking core, vendored verbatim from
# ../picoos/kernel/task.{h,cpp} -- see the picoos-style-migration-goal memory
# note / README for the plan this is step 1 of. Kept as its own library
# target, not linked into `gateway` yet, so gateway.cpp stays untouched.
add_library(picogateway_kernel
        kernel/task.cpp
        kernel/klog.cpp
        kernel/fs.cpp
        kernel/prog.cpp
        )
target_include_directories(picogateway_kernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(picogateway_kernel pico_stdlib)

# demo_sched: step 1 -- two tasks, no gateway hardware touched, proving the
# scheduler out on this board before any subsystem is migrated onto it.
add_executable(demo_sched
        demo_sched.cpp
        )
target_link_libraries(demo_sched picogateway_kernel pico_stdlib)
pico_enable_stdio_usb(demo_sched 1)
pico_enable_stdio_uart(demo_sched 0)
pico_add_extra_outputs(demo_sched)

# demo_klog: step 2 -- proves serialized cross-task logging (klog/klog_flush)
# before any gateway task starts relying on it.
add_executable(demo_klog
        demo_klog.cpp
        )
target_link_libraries(demo_klog picogateway_kernel pico_stdlib)
pico_enable_stdio_usb(demo_klog 1)
pico_enable_stdio_uart(demo_klog 0)
pico_add_extra_outputs(demo_klog)

# demo_fs: step 3 -- the device namespace (kernel/fs.h), pointed at a real
# device for the first time: /dev/buttons, backed by the actual button
# GPIOs + debounce timer carved out of gateway.cpp into buttons.cpp. First
# real gateway subsystem to move onto the new architecture.
add_executable(demo_fs
        demo_fs.cpp
        buttons.cpp
        dev/buttons.cpp
        )
target_link_libraries(demo_fs picogateway_kernel pico_stdlib hardware_gpio)
pico_enable_stdio_usb(demo_fs 1)
pico_enable_stdio_uart(demo_fs 0)
pico_add_extra_outputs(demo_fs)

# demo_shell: step 4 -- the interactive shell (jobs.{h,cpp}, shell.cpp,
# kernel/prog.h + cat/echo/ls), pipes/redirects/background jobs on top of
# the real /dev/buttons from step 3. First step you can actually type at.
add_executable(demo_shell
        demo_shell.cpp
        buttons.cpp
        dev/buttons.cpp
        jobs.cpp
        shell.cpp
        prog/cat.cpp
        prog/echo.cpp
        prog/ls.cpp
        )
target_link_libraries(demo_shell picogateway_kernel pico_stdlib hardware_gpio)
pico_enable_stdio_usb(demo_shell 1)
pico_enable_stdio_uart(demo_shell 0)
pico_add_extra_outputs(demo_shell)

# demo_room: step 5 -- room sensors (DHT22 temp+hum, light ADC) carved
# out of gateway.cpp into room.cpp, exposed as /dev/room/temp,
# /dev/room/hum, /dev/room/light (dev/room.cpp), polled periodically by
# a real kernel/task.h task instead of gateway.cpp's hand-rolled
# SENSOR_INTERVAL_TIME gate. Shell + cat/echo/ls again so the cached
# readings can be inspected interactively.
add_executable(demo_room
        demo_room.cpp
        room.cpp
        dev/room.cpp
        jobs.cpp
        shell.cpp
        prog/cat.cpp
        prog/echo.cpp
        prog/ls.cpp
        )
target_link_libraries(demo_room picogateway_kernel pico_stdlib hardware_adc dht)
pico_enable_stdio_usb(demo_room 1)
pico_enable_stdio_uart(demo_room 0)
pico_add_extra_outputs(demo_room)

# demo_config: step 6 -- the config EEPROM (24CS16) carved out of
# gateway.cpp into eeprom.cpp, exposed read/write as
# /dev/config/{wifi_ssid,wifi_pass,url,farm,api0..api3} (dev/config.cpp),
# with write-verify + read-validate corruption checks gateway.cpp never
# had. Shell + cat/echo/ls again so fields can be read and set
# interactively.
add_executable(demo_config
        demo_config.cpp
        eeprom.cpp
        dev/config.cpp
        jobs.cpp
        shell.cpp
        prog/cat.cpp
        prog/echo.cpp
        prog/ls.cpp
        )
target_link_libraries(demo_config picogateway_kernel pico_stdlib hardware_i2c hardware_gpio)
pico_enable_stdio_usb(demo_config 1)
pico_enable_stdio_uart(demo_config 0)
pico_add_extra_outputs(demo_config)

# demo_lora: step 7 -- the LoRa field probes (lora.cpp, carved out of
# gateway.cpp's LoRa.begin()/parsePacket() loop), exposed read-only as
# /dev/lora/probe{1,2,3}/{temp,soil,ph,rssi,age} (dev/lora.cpp). First
# subsystem with no picoos precedent to lean on. Shell + cat/echo/ls
# again so cached readings can be inspected interactively.
add_executable(demo_lora
        demo_lora.cpp
        lora.cpp
        dev/lora.cpp
        jobs.cpp
        shell.cpp
        prog/cat.cpp
        prog/echo.cpp
        prog/ls.cpp
        )
target_include_directories(demo_lora PRIVATE ../LoRa-pi-pico/src)
target_link_libraries(demo_lora picogateway_kernel pico_stdlib LoRa_pico_lib hardware_spi hardware_interp LoRa_print)
pico_enable_stdio_usb(demo_lora 1)
pico_enable_stdio_uart(demo_lora 0)
pico_add_extra_outputs(demo_lora)

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        -Wno-maybe-uninitialized
)
