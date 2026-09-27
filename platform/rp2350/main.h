#ifndef QRP2350_ENGINE_MAIN_H
#define QRP2350_ENGINE_MAIN_H
/* RP2350 platform configuration shim for MG24-derived Quake sources.
 *
 * Some engine sources include "main.h" for a small set of build/configuration
 * constants.  The original QuakeMG24/src/main.h also owns the EFR32 hardware
 * configuration and therefore includes Silicon Labs generated headers.  That
 * header must never be used by an RP2350 target.
 *
 * Keep this file deliberately small: add only board-independent definitions
 * that an engine source actually requires.  RP2350 clocks, DMA, timers, LCD,
 * input and audio belong to the Pico platform layer, not here.
 */
#include <stdbool.h>
#include <stdint.h>

#ifndef QUAKE_RP2350
#define QUAKE_RP2350 1
#endif

/* CMSIS normally supplies __ASM in the EFR32 build.  The RP2350 GCC target is
 * also ARM and supports the same inline-assembly syntax used by the engine
 * (e.g. USAT), so provide only the compiler spelling here instead of importing
 * the Silicon Labs/CMSIS platform header tree.
 */
#ifndef __ASM
#define __ASM __asm
#endif

/* Engine feature/configuration values inherited from the MG24 build. */
#define FAST_CPU_SMALL_FLASH              0
#define CORRECT_TABLE_ERROR               1
#define DISABLE_CACHING_TEXTURE_TO_FLASH  0
#define TEST_DISABLE_ASYNCH_LOAD          0
#define SPI_FLASH_32BIT_ADDRESS           1
#define PAK_FILE_NAME                     "PAK0.PAK"
#define DEBUG_OUT_PRINTF                  1

/* On RP2350 immutable assets are memory-mapped XIP; this section attribute is
 * an MG24 SRAM-placement policy and must not leak into the Pico linker layout.
 */
#define AUX_SECTION

/* Kept for MG24-derived code that uses the timing helper declaration. */
unsigned int I_GetTimeMicrosecs(void);

#endif /* QRP2350_ENGINE_MAIN_H */
