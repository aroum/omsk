cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(omsk_grain C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(PICO_BOARD STREQUAL "pico")
    set(PICO_FLASH_SIZE_BYTES 2097152)  # 2 MB for RP2040
else()
    set(PICO_FLASH_SIZE_BYTES 4194304)  # 4 MB for RP2350
endif()
pico_sdk_init()

# u8g2 graphics library
set(U8G2_DIR ${CMAKE_CURRENT_LIST_DIR}/../shared/lib/u8g2/csrc)
file(GLOB U8G2_SRC "${U8G2_DIR}/*.c")

add_executable(omsk_grain
    src/main.cpp
    src/synth/filter.cpp
    src/synth/grain.cpp
    src/synth/grain_env.cpp
    src/synth/fx.cpp
    src/synth/audio_engine.cpp
    src/synth/synth.cpp
    src/ui/ui_state.cpp
    src/ui/ui_oled.cpp
    src/leds/leds.cpp
    src/midi/midi_map.cpp
    src/usb/usb_descriptors.cpp
    src/usb/usb_midi.cpp
    ../shared/hardware/matrix.cpp
    ../shared/hardware/encoders.cpp
    ../shared/hardware/midi_uart.cpp
    ../shared/dsp/fast_math.cpp
    ${U8G2_SRC}
)

pico_generate_pio_header(omsk_grain ${CMAKE_CURRENT_LIST_DIR}/../shared/pio/audio.pio)
pico_generate_pio_header(omsk_grain ${CMAKE_CURRENT_LIST_DIR}/../shared/pio/i2s_tx.pio)
pico_generate_pio_header(omsk_grain ${CMAKE_CURRENT_LIST_DIR}/../shared/pio/ws2812.pio)

target_include_directories(omsk_grain PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/tables
    ${CMAKE_CURRENT_LIST_DIR}/src/ui
    ${CMAKE_CURRENT_LIST_DIR}/src/leds
    ${CMAKE_CURRENT_LIST_DIR}/src/midi
    ${CMAKE_CURRENT_LIST_DIR}/src/usb
    ${CMAKE_CURRENT_LIST_DIR}/../shared/hardware
    ${CMAKE_CURRENT_LIST_DIR}/../shared/dsp
    ${U8G2_DIR}
)

# Link libraries
target_link_libraries(omsk_grain
    pico_stdlib
    pico_multicore
    pico_rand
    hardware_pio
    hardware_dma
    hardware_pwm
    hardware_irq
    hardware_adc
    hardware_uart
    hardware_i2c
    hardware_clocks
    hardware_flash
    tinyusb_device
    tinyusb_board
)

# Size and speed optimizations
target_compile_options(omsk_grain PRIVATE
    -O2
    -ffast-math
    -ffunction-sections
    -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    # Put audio LUT data in flash (handled by PICO_FLASH_SIZE_BYTES variable)
    # No explicit -DPICO_FLASH_SIZE_BYTES definition needed
)

target_link_options(omsk_grain PRIVATE
    -Wl,--gc-sections
)

pico_enable_stdio_usb(omsk_grain 0)
pico_enable_stdio_uart(omsk_grain 1)

pico_add_extra_outputs(omsk_grain)