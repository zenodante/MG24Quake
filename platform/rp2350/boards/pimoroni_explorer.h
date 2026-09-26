/*
 * Board definition for Pimoroni Explorer (PIM720).
 * Derived from the adjacent Pimoroni_Explorer_Schematic.pdf.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _BOARDS_PIMORONI_EXPLORER_H
#define _BOARDS_PIMORONI_EXPLORER_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define PIMORONI_EXPLORER

/* The Explorer uses the 80-GPIO RP2350B package. */
#define PICO_RP2350A 0

/* LCD: ST7789-compatible 8-bit parallel bus. */
#define PIMORONI_EXPLORER_LCD_BACKLIGHT_PIN 26
#define PIMORONI_EXPLORER_LCD_CS_PIN        27
#define PIMORONI_EXPLORER_LCD_RS_PIN        28
#define PIMORONI_EXPLORER_LCD_WR_PIN        30
#define PIMORONI_EXPLORER_LCD_RD_PIN        31
#define PIMORONI_EXPLORER_LCD_DB0_PIN       32

/* Treat the active-high LCD backlight as the board LED for bring-up. */
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN PIMORONI_EXPLORER_LCD_BACKLIGHT_PIN
#endif
#ifndef PICO_DEFAULT_LED_PIN_INVERTED
#define PICO_DEFAULT_LED_PIN_INVERTED 0
#endif

/* External 16 MB Winbond W25Q128JVPIQ flash. */
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
