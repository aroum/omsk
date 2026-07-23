cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(omsk_oled_test C CXX ASM)
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

add_executable(omsk_oled_test
    src/main.cpp
    ${U8G2_SRC}
)

target_include_directories(omsk_oled_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/../shared
    ${U8G2_DIR}
)

# Link libraries
target_link_libraries(omsk_oled_test
    pico_stdlib
    hardware_i2c
)

# Size and speed optimizations
target_compile_options(omsk_oled_test PRIVATE
    -O2
    -ffast-math
    -ffunction-sections
    -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

target_link_options(omsk_oled_test PRIVATE
    -Wl,--gc-sections
)

pico_enable_stdio_usb(omsk_oled_test 0)
pico_enable_stdio_uart(omsk_oled_test 1)

pico_add_extra_outputs(omsk_oled_test)
