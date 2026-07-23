cmake_minimum_required(VERSION 3.13)

set(PICO_TINYUSB_PATH ${CMAKE_CURRENT_LIST_DIR}/../shared/lib/tinyusb)
include(pico_sdk_import.cmake)

project(omsk_fm C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(PICO_BOARD STREQUAL "pico")
    set(PICO_FLASH_SIZE_BYTES 2097152)  # 2 MB for RP2040
else()
    set(PICO_FLASH_SIZE_BYTES 4194304)  # 4 MB for RP2350
endif()
pico_sdk_init()

# Add CMSIS-DSP
add_subdirectory(../shared/lib/CMSIS-DSP CMSIS-DSP)
target_include_directories(CMSISDSP PRIVATE ${PICO_SDK_PATH}/src/rp2_common/cmsis/stub/CMSIS/Core/Include)
target_compile_options(CMSISDSP PRIVATE -O0)

add_executable(omsk_fm
    src/main.cpp
    src/synth/synth.cpp
    src/synth/audio.cpp
    src/synth/fm_synth.cpp
    src/tables/dx7_tables.cpp
    src/synth/pra_synth.cpp
    src/midi/midi_handler.cpp
    src/midi/midi_map.cpp
    src/ui/ui_oled.cpp
    src/ui/ui_logic.cpp
    ../shared/hardware/matrix.cpp
    ../shared/hardware/encoders.cpp
    ../shared/hardware/midi_uart.cpp
    ../shared/dsp/fast_math.cpp
    src/leds/leds.cpp
    src/sequencer/sequencer.cpp
    src/usb/usb_descriptors.c
    src/usb/usb_midi.cpp
)

# Generate PIO headers
pico_generate_pio_header(omsk_fm ${CMAKE_CURRENT_LIST_DIR}/../shared/pio/audio.pio)
pico_generate_pio_header(omsk_fm ${CMAKE_CURRENT_LIST_DIR}/../shared/pio/ws2812.pio)
pico_generate_pio_header(omsk_fm ${CMAKE_CURRENT_LIST_DIR}/../shared/pio/i2s_tx.pio)
pico_generate_pio_header(omsk_fm ${CMAKE_CURRENT_LIST_DIR}/../shared/pio/button_matrix.pio)

# Add include directories
target_include_directories(omsk_fm PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/usb
    ${CMAKE_CURRENT_LIST_DIR}/src/synth
    ${CMAKE_CURRENT_LIST_DIR}/src/tables
    ${CMAKE_CURRENT_LIST_DIR}/src/ui
    ${CMAKE_CURRENT_LIST_DIR}/src/leds
    ${CMAKE_CURRENT_LIST_DIR}/src/midi
    ${CMAKE_CURRENT_LIST_DIR}/src/sequencer
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../shared/lib/CMSIS-DSP/Include
    ${CMAKE_CURRENT_LIST_DIR}/../shared/hardware
    ${CMAKE_CURRENT_LIST_DIR}/../shared/dsp
    ${PICO_SDK_PATH}/src/rp2_common/cmsis/stub/CMSIS/Core/Include
)

set(U8G2_DIR ${CMAKE_CURRENT_LIST_DIR}/../shared/lib/u8g2)
if (EXISTS ${U8G2_DIR}/csrc)
    message(STATUS "u8g2 detected at ${U8G2_DIR}")
    target_include_directories(omsk_fm PUBLIC ${U8G2_DIR}/csrc)
    file(GLOB U8G2_SRC ${U8G2_DIR}/csrc/*.c)
    target_sources(omsk_fm PRIVATE ${U8G2_SRC})
endif()

# Link libraries
target_link_libraries(omsk_fm
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

# Size optimizations
target_compile_options(omsk_fm PRIVATE
    -O2
    -ffast-math
    -ffunction-sections
    -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>  # C++ only: no exception tables
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>        # C++ only: no RTTI overhead
)
target_link_options(omsk_fm PRIVATE
    -Wl,--gc-sections
    -Wl,--strip-debug   # Remove debug symbols from final binary
)

# Enable USB output, disable UART output
pico_enable_stdio_usb(omsk_fm 0)
pico_enable_stdio_uart(omsk_fm 0)

# Create map/bin/hex/uf2
pico_add_extra_outputs(omsk_fm)
