/* Phase-1 runtime probe for the original MG24 Quake model loader.
 *
 * This is intentionally narrow: mount the production QXIP image, initialize
 * the RP2350 SRAM replacement for MG24's internal-flash object store, then run
 * the real Mod_Init -> Mod_ForName -> Mod_LoadModel -> Mod_LoadBrushModel path
 * for maps/start.bsp.  It does not start the renderer/game or Core 1 yet.
 */
#include "quakedef.h"
#include "r_local.h"
#include "d_local.h"
#include "sys.h"
#include "qpak.h"
#include "qfiles.h"
#include "qlevel_arena.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

_Static_assert(sizeof(uint8_t) == 1, "Quake assets require 8-bit bytes");
_Static_assert(sizeof(uint16_t) == 2, "Quake assets require 16-bit words");
_Static_assert(sizeof(uint32_t) == 4, "Quake assets require 32-bit words");
_Static_assert(sizeof(float) == 4, "MG24 renderer assumes IEEE-754 binary32");

static qpak_t pak;
void qmodel_phase1_init(void);

/* Kept non-static so the linker/map file exposes the engine ABI checkpoint. */
size_t qengine_phase1_abi_probe(void)
{
    return sizeof(model_t) + sizeof(msurface_t) + sizeof(mplane_t) +
           sizeof(medge_t) + sizeof(entity_t) + sizeof(espan_t);
}

static void checkpoint(unsigned n, const char *message)
{
    printf("[phase1:%u] %s\n", n, message);
    stdio_flush();
}

int main(void)
{
    vreg_set_voltage(VREG_VOLTAGE_1_10);
    set_sys_clock_pll(1200000000u, 6, 1); /* 200 MHz, same as bring-up. */
    stdio_init_all();
    sleep_ms(1500);

    printf("\nRP2350 MG24 model-loader runtime probe\n");
    printf("ABI bytes=%lu level_arena=%lu\n",
           (unsigned long)qengine_phase1_abi_probe(),
           (unsigned long)qlevel_arena_capacity());
    printf("loader structs: model=%u surface=%u face-temp=%u bytes stack=%u\n",
           (unsigned)sizeof(model_t), (unsigned)sizeof(msurface_t),
           (unsigned)(sizeof(msurface_t) * 128u), (unsigned)PICO_STACK_SIZE);

    checkpoint(1, "opening QXIP at flash +1 MiB");
    if (!qpak_open(&pak, (const void *)(XIP_BASE + QPAK_ASSET_OFFSET),
                   QPAK_ASSET_CAPACITY))
        panic("phase1: QXIP open failed");
    printf("QXIP bytes=%lu files=%lu textures=%lu\n",
           (unsigned long)pak.bytes, (unsigned long)pak.files,
           (unsigned long)pak.texture_count);
    qfiles_mount(&pak);

    checkpoint(2, "mapping maps/start.bsp through production file ABI");
    unsigned int bsp_bytes = 0;
    byte *bsp = getExtMemPointerToFileInPak("maps/start.bsp", &bsp_bytes);
    if (!bsp || bsp_bytes < sizeof(dheader_t))
        panic("phase1: start.bsp missing/short");
    printf("start.bsp xip=%p bytes=%u version=%d\n",
           (void *)bsp, bsp_bytes, ((dheader_t *)bsp)->version);

    checkpoint(3, "initializing RP2350 level SRAM arena");
    internalFlashInit();
    qmodel_phase1_init();
    printf("arena used=%lu common=%lu remaining=%lu\n",
           (unsigned long)qlevel_arena_used(),
           (unsigned long)qlevel_arena_common_bytes(),
           (unsigned long)(qlevel_arena_capacity() - qlevel_arena_used()));

    checkpoint(4, "calling real MG24 Mod_Init");
    Mod_Init();
    printf("after Mod_Init: arena used=%lu common=%lu\n",
           (unsigned long)qlevel_arena_used(),
           (unsigned long)qlevel_arena_common_bytes());

    checkpoint(5, "calling Mod_ForName maps/start.bsp");
    model_t *world = Mod_ForName("maps/start.bsp", true);
    if (!world)
        panic("phase1: Mod_ForName returned NULL");

    checkpoint(6, "real MG24 brush-model load returned");
    printf("model=%p type=%d frames=%d arena=%lu/%lu common=%lu\n",
           (void *)world, (int)world->type, world->numframes,
           (unsigned long)qlevel_arena_used(),
           (unsigned long)qlevel_arena_capacity(),
           (unsigned long)qlevel_arena_common_bytes());

    checkpoint(7, "start.bsp Phase-1 load SUCCESS");
    printf("Leave the board running; no renderer/game loop is started by this probe.\n");
    for (;;) {
        tight_loop_contents();
    }
}
