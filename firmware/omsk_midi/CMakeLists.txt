cmake_minimum_required(VERSION 3.13)

# Initialize pico-sdk
include(pico_sdk_import.cmake)

project(omsk_midi C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

add_executable(omsk_midi
    src/main.c
    src/usb/usb_descriptors.c
    ../shared/hardware/matrix.cpp
    ../shared/hardware/encoders.cpp
    ../shared/hardware/midi_uart.cpp
)

# Add PIO generation
pico_generate_pio_header(omsk_midi ${CMAKE_CURRENT_LIST_DIR}/../shared/pio/ws2812.pio)

# Add include paths
target_include_directories(omsk_midi PRIVATE
    src
    src/usb
    ${CMAKE_CURRENT_LIST_DIR}/../shared/hardware
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Pull in common dependencies
target_link_libraries(omsk_midi 
    pico_stdlib 
    hardware_pio 
    hardware_dma 
    tinyusb_device 
    tinyusb_board
)

# Enable USB MIDI
target_compile_definitions(omsk_midi PRIVATE
    CFG_TUSB_MCU=OPT_MCU_RP2040
    CFG_TUD_MIDI=1
    CFG_TUD_CDC=0
    CFG_TUD_MSC=0
)

# Enable usb output
pico_enable_stdio_usb(omsk_midi 1)
pico_enable_stdio_uart(omsk_midi 0)

# Create map/bin/hex/uf2 file
pico_add_extra_outputs(omsk_midi)
