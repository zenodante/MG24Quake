/* Phase-1 runtime probe for the original MG24 Quake model loader.
 *
 * This is deliberately not a second renderer or engine.  It mounts the same
 * immutable QXIP image used by the validated bringup firmware and then enters
 * the real MG24 Mod_Init -> Mod_ForName -> Mod_LoadModel -> Mod_LoadBrushModel
 * path.  Serial checkpoints make the first remaining platform assumption
 * visible on hardware.
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

/* Kept non-static so the linker/map file exposes the engine ABI checkpoint. */
size_t qengine_phase1_abi_probe(void)
{
    return sizeof(model_t) + sizeof(msurface_t) + sizeof(mplane_t) +
           sizeof(medge_t) + sizeof(entity_t) + sizeof(espan_t);
}

static qpak_t pak;

int main(void)
{
    vreg_set_voltage(VREG_VOLTAGE_1_10);
    set_sys_clock_pll(1200000000u, 6, 1);
    stdio_init_all();
    sleep_ms(1500); /* give USB CDC time to enumerate before the first checkpoint */

    printf("\nRP2350 MG24 model-loader probe\n");
    printf("core0=%luMHz abi=%lu level_arena=%lu\n",
           (unsigned long)(clock_get_hz(clk_sys) / 1000000u),
           (unsigned long)qengine_phase1_abi_probe(),
           (unsigned long)qlevel_arena_capacity());

    bool assets = qpak_open(&pak,
                            (const void *)(XIP_BASE + QPAK_ASSET_OFFSET),
                            QPAK_ASSET_CAPACITY);
    printf("[1] qxip open: %s", assets ? "OK" : "FAIL");
    if (assets)
        printf(" bytes=%lu files=%lu textures=%lu format=%u",
               (unsigned long)pak.bytes, (unsigned long)pak.files,
               (unsigned long)pak.texture_count, (unsigned)pak.format);
    printf("\n");
    if (!assets)
        panic("Phase1: QXIP open failed");

    qfiles_mount(&pak);
    unsigned int start_bytes = 0;
    byte *start = getExtMemPointerToFileInPak("maps/start.bsp", &start_bytes);
    printf("[2] start.bsp map: %s ptr=%p bytes=%u\n",
           start ? "OK" : "FAIL", (void *)start, start_bytes);
    if (!start)
        panic("Phase1: maps/start.bsp missing");

    internalFlashInit();
    printf("[3] level arena init: used=%lu common=%lu remaining=%lu\n",
           (unsigned long)qlevel_arena_used(),
           (unsigned long)qlevel_arena_common_bytes(),
           (unsigned long)(qlevel_arena_capacity() - qlevel_arena_used()));

    printf("[4] Mod_Init begin\n");
    Mod_Init();
    printf("[5] Mod_Init OK; Mod_ForName(start) begin\n");

    model_t *world = Mod_ForName("maps/start.bsp", true);
    printf("[6] Mod_ForName returned %p\n", (void *)world);
    if (!world)
        panic("Phase1: Mod_ForName returned NULL");

    printf("[7] start loaded: type=%d surfaces=%d nodes=%d leafs=%d submodels=%d\n",
           (int)world->type, world->numsurfaces, world->numnodes,
           world->numleafs, world->numsubmodels);
    printf("[8] level arena: used=%lu common=%lu remaining=%lu\n",
           (unsigned long)qlevel_arena_used(),
           (unsigned long)qlevel_arena_common_bytes(),
           (unsigned long)(qlevel_arena_capacity() - qlevel_arena_used()));
    printf("PHASE1 MODEL LOAD PASS\n");

    for (;;)
        tight_loop_contents();
}
