file(GLOB engine_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../../../QuakeMG24/Quake/*.c")
list(FILTER engine_sources EXCLUDE REGEX "/(vid_sdl|snd_sdl|debug|snd_dma|snd_mem|snd_mix|d_surf|r_sky|internalFlash|common)\\.c$")
# A private copy prevents the phase-1 probe's legacy symbol rename leaking in.
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../../QuakeMG24/Quake/common.c" "${CMAKE_CURRENT_BINARY_DIR}/common.c" COPYONLY)
add_executable(quake_rp2350 ${engine_sources} "${CMAKE_CURRENT_BINARY_DIR}/common.c"
    platform.c resources.c metadata.c diagnostics.c
    sound.c ../../macos/game_surface.c
    ../qpak.c ../qmix.c ../qservice.c ../boards/st7789.c ../boards/qwstpad.c
    ../../../QuakeMG24/src/font8x8.c)
target_include_directories(quake_rp2350 PRIVATE . .. ../boards ../../macos ../../../QuakeMG24/Quake ../../../QuakeMG24/src)
target_compile_options(quake_rp2350 PRIVATE -Os -ffp-contract=off -fstack-usage
    -Wno-incompatible-pointer-types -Wno-int-conversion -Wno-discarded-qualifiers
    "-include${CMAKE_CURRENT_LIST_DIR}/config.h")
target_compile_definitions(quake_rp2350 PRIVATE QUAKE_RP2350=1 PICO_STACK_SIZE=0x8000 PICO_CORE1_STACK_SIZE=0x1000 PICO_FLASH_SPI_CLKDIV=4)
target_link_options(quake_rp2350 PRIVATE -Wl,--gc-sections -Wl,--defsym=HEAP_LIMIT=0x20078000)
target_link_libraries(quake_rp2350 PRIVATE pico_stdlib pico_multicore hardware_clocks hardware_vreg hardware_dma hardware_i2c hardware_pio hardware_pwm m)
pico_add_linker_script_override_path(quake_rp2350 "${CMAKE_CURRENT_BINARY_DIR}/../firmware-layout" FILES pico_flash_region.ld)
pico_generate_pio_header(quake_rp2350 "${CMAKE_CURRENT_LIST_DIR}/../boards/st7789_parallel.pio")
pico_enable_stdio_usb(quake_rp2350 1)
pico_enable_stdio_uart(quake_rp2350 0)
pico_add_extra_outputs(quake_rp2350)
add_custom_command(TARGET quake_rp2350 POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../../../Tools/RP2350Pack/check_firmware.py" "$<TARGET_FILE_DIR:quake_rp2350>/quake_rp2350.bin"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../../../Tools/RP2350Pack/make_uf2.py" --firmware-only "$<TARGET_FILE_DIR:quake_rp2350>/quake_rp2350.uf2" "$<TARGET_FILE_DIR:quake_rp2350>/quake_rp2350.uf2")

pico_add_linker_script_override_path(quake_rp2350 "${CMAKE_CURRENT_LIST_DIR}" FILES sections_stack.incl section_end.incl)
