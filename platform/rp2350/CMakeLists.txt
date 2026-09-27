cmake_minimum_required(VERSION 3.20)
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Target platform")
set(PICO_BOARD pimoroni_explorer CACHE STRING "Board")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/boards")
if(NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()
if(NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    message(FATAL_ERROR "Set PICO_SDK_PATH to a local Pico SDK checkout")
endif()
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
project(quake_rp2350 C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(QRP_PLATFORM_SOURCES
    qpak.c qfiles.c qmix.c qservice.c
    boards/st7789.c boards/qwstpad.c ../../QuakeMG24/src/font8x8.c)

# Existing hardware/resource diagnostic.  Keep this while Phase 1 brings the
# real MG24 engine up; qrender/qbsp/qcollision are not the production renderer.
add_executable(quake_rp2350_bringup
    main.c qbsp.c qrender.c qcollision.c ${QRP_PLATFORM_SOURCES})

# Phase-1 engine integration checkpoint.  This compiles against the actual
# MG24 Quake engine ABI and the RP2350 file/XIP + core-1 service layer without
# creating another renderer.  Engine source groups are added here incrementally
# as EFR32-only dependencies are removed.
add_library(quake_rp2350_engine_phase1 OBJECT
    qengine_probe.c qpak.c qfiles.c)
target_include_directories(quake_rp2350_engine_phase1 PRIVATE
    . boards ../../QuakeMG24/src ../../QuakeMG24/Quake)
target_compile_options(quake_rp2350_engine_phase1 PRIVATE -Wall -Wextra -O2)
target_compile_definitions(quake_rp2350_engine_phase1 PRIVATE QUAKE_RP2350=1)

# Limit linkable Flash without changing the board's physical 16 MiB capacity.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/flash_layout.h")
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/flash_layout.h" firmware_limit
    REGEX "^#define QRP_FIRMWARE_BYTES")
string(REGEX MATCH "0x[0-9a-fA-F]+" firmware_limit "${firmware_limit}")
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/flash_layout.h" xip_base
    REGEX "^#define QRP_XIP_BASE")
string(REGEX MATCH "0x[0-9a-fA-F]+" xip_base "${xip_base}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/firmware-layout")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/firmware-layout/pico_flash_region.ld"
    "FLASH(rx) : ORIGIN = ${xip_base}, LENGTH = ${firmware_limit}\n")
pico_add_linker_script_override_path(quake_rp2350_bringup
    "${CMAKE_CURRENT_BINARY_DIR}/firmware-layout" FILES pico_flash_region.ld)
target_include_directories(quake_rp2350_bringup PRIVATE . boards ../../QuakeMG24/src)
target_compile_options(quake_rp2350_bringup PRIVATE -Wall -Wextra -O2 -fstack-usage)
target_compile_definitions(quake_rp2350_bringup PRIVATE
    PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0x1000 PICO_FLASH_SPI_CLKDIV=4)
target_link_libraries(quake_rp2350_bringup PRIVATE pico_stdlib pico_multicore
    hardware_clocks hardware_vreg hardware_dma hardware_i2c hardware_pio hardware_pwm)
pico_generate_pio_header(quake_rp2350_bringup "${CMAKE_CURRENT_LIST_DIR}/boards/st7789_parallel.pio")
pico_enable_stdio_usb(quake_rp2350_bringup 1)
pico_enable_stdio_uart(quake_rp2350_bringup 0)
pico_add_extra_outputs(quake_rp2350_bringup)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(TARGET quake_rp2350_bringup POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../../Tools/RP2350Pack/check_firmware.py"
        "${CMAKE_CURRENT_BINARY_DIR}/quake_rp2350_bringup.bin"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../../Tools/RP2350Pack/make_uf2.py"
        --firmware-only "${CMAKE_CURRENT_BINARY_DIR}/quake_rp2350_bringup.uf2"
        "${CMAKE_CURRENT_BINARY_DIR}/quake_rp2350_bringup.uf2")
set_property(TARGET quake_rp2350_bringup APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../../Tools/RP2350Pack/make_uf2.py"
    "${CMAKE_CURRENT_LIST_DIR}/../../Tools/RP2350Pack/flash_layout.py"
    "${CMAKE_CURRENT_LIST_DIR}/../../Tools/RP2350Pack/check_firmware.py")
