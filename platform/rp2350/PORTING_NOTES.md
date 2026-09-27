# RP2350 Quake port plan

Reference: [next-hack, original MG24 Quake port, 2024-09-22](https://next-hack.com/index.php/2024/09/22/quake-port-to-sparkfun-and-arduino-nano-matter-boards-using-only-276-kb-ram/).

Central rule: **MG24 is the engine/renderer baseline, not the RP2350 hardware baseline.** Keep the author's Quake algorithm and performance work; replace hardware and memory compromises that existed only because MG24 had 276 kB RAM and external SPI storage.

## Fixed RP2350 architecture

The Core-1 service architecture is validated and is frozen unless integration exposes a measured defect.

- Core 0 runs the Quake engine and MG24-derived software renderer.
- One 320x200 8-bit indexed framebuffer (64,000 bytes).
- Core 1 owns display conversion/output, input polling and audio mixing/output.
- Display conversion uses two independent 320-pixel RGB565 row buffers, one row per DMA transfer. Do not reintroduce multi-row staging buffers or a second full framebuffer.
- Keep the existing qservice/qmix/board-driver ownership model.
- The single framebuffer has an ownership barrier: Core 0 renders only while Core 1 is not consuming it.

## Renderer decision

`platform/rp2350/qrender.c` is a bring-up/reference rasterizer only. Its triangle fan, bounding-box walk, per-pixel barycentric floating point, perspective division and light interpolation must not evolve into the production renderer.

Production uses the existing MG24 Quake pipeline: `r_main/r_bsp/r_edge/r_surf/r_sky`, `d_edge/d_scan/d_surf`, and related sprite/alias paths. Preserve the edge/surface/span architecture, fixed-point and packed Cortex-M optimizations, especially the optimized `D_DrawSpans8` family.

`qbsp.c`, `qcollision.c` and `qrender.c` remain temporarily for hardware diagnostics. They are not a parallel production engine and leave the production build as the corresponding MG24 paths become functional.

## RP2350 memory/storage policy

RP2350 has 520 kB SRAM, two cores and 16 MiB memory-mapped XIP flash.

- Spend SRAM to reduce CPU/XIP traffic: renderer working sets, hot tables, surface/texture caches and mutable model state are candidates.
- Keep immutable bulk resources in XIP where layout permits safe efficient access.
- Do not preserve MG24 RAM aliasing solely to save a few kB when it complicates asynchronous Core-1 ownership.
- Do not preserve MG24 external-SPI interleaving or EFR32 display/audio/input/DMA/flash code.
- Level changes must never populate a second runtime flash cache.

Preferred resource hierarchy is direct contiguous XIP pointer, then bounded XIP reader, then SRAM cache/copy for hot or mutable data, then host-derived representation when it removes measured runtime cost.

## Host tools

`Tools/MCUPackConverter` contains useful MG24 transformations (alias triangle data, sky preprocessing, BSP modification, sound resampling) mixed with MG24 flash/RAM workarounds. Audit each consumer individually.

`Tools/RP2350Pack/pack.py` is the lossless diagnostic QRP container and may compress texture/lightmap pages; compressed pages are therefore not the default production hot-resource format.

`Tools/RP2350Pack/prepare_xip.py` provides the production staging baseline: merge pak0 plus optional pak1 with later-PAK override semantics, keep data uncompressed, align entries for XIP, and perform no runtime level-cache writes.

Classify every MG24 conversion dependency as: speed transformation (retain host-side), RAM workaround (normally remove/use SRAM), external-flash workaround (remove), or format convenience (choose by measured XIP/runtime cost).

## Migration phases and status

### Phase 0 - validated platform services: COMPLETE/FROZEN

qservice/qmix, ST7789 DMA, one-line RGB565 ping-pong conversion, input and audio on Core 1 are the known-good platform layer.

### Phase 1 - build the real MG24 engine on RP2350: IN PROGRESS

Goal: compile the MG24 Quake engine against a thin RP2350 HAL, initialize the engine and load `start` from immutable XIP without level-time flash programming.

Implemented checkpoints:

1. `quake_rp2350_engine_phase1` object target now exists beside the old diagnostic executable. This is the migration target; the diagnostic remains available for hardware regression testing.
2. The Phase-1 target includes the actual `QuakeMG24/Quake` headers and compiles an ABI probe against `quakedef.h`, `r_local.h`, `d_local.h` and `sys.h`. This intentionally exposes MG24 assumptions rather than hiding them behind a new renderer/data model.
3. Existing `qfiles.c` already implements the Quake `Sys_FileOpenRead/Close/Seek/Read/Time` API over qpak and supports `qfiles_map()` for contiguous immutable XIP ranges. This is the initial read-only file HAL. Write APIs remain disabled by design.
4. The RP2350 host tooling now has an uncompressed XIP staging path (`prepare_xip.py`), so Phase 1 does not depend on level-time internal-flash caching.
5. Core-1 service code is not linked into or modified by this checkpoint; its interface remains the integration boundary.

Next Phase-1 work, in order:

- Compile the MG24 engine source groups into `quake_rp2350_engine_phase1` and inventory compile failures by category: EFR32 hardware dependency, converted-PAK dependency, MG24 flash-cache dependency, or ordinary platform HAL dependency.
- Implement RP2350 `Sys_FloatTime`, console/error stubs and remaining non-file `sys.h` boundary functions without importing EFR32 code.
- Audit `model.c` first: remove `storeToInternalFlash`/level-cache assumptions and adapt model/BSP loading to immutable XIP plus bounded SRAM metadata while preserving speed-relevant converted representations.
- Audit the generated QuakeC/entity path and allocator assumptions against RP2350 SRAM/address width; retain compact representation where it remains valid.
- Add the production entry point only after the engine object set compiles cleanly. The first runtime milestone is engine initialization + `start` load, not world rendering.

Phase 1 is **not yet complete**: the real engine source set is not yet linked and `start` is not yet loaded through `model.c`. Do not report the diagnostic BSP loader as this milestone.

### Phase 2 - restore MG24 world rendering: NOT STARTED

Bring up BSP/PVS -> edge -> surface -> span rendering into the single indexed framebuffer. Core 1 displays completed frames unchanged. First visual milestone is `start` rendered through MG24, not `qrender.c`, with Core-0 render time measured independently of LCD transfer.

### Phase 3 - model/collision/game integration: NOT STARTED

Use MG24/Quake model/hull structures and normal engine collision. Retire qbsp/qcollision from production as replacements become functional. Restore brush entities, alias models, sprites, particles, sky, turbulence, dynamic lighting and gameplay incrementally.

### Phase 4 - proven host-side hot formats: NOT STARTED

Port only speed-relevant MCUPackConverter transformations. Generate derived bytes once on the PC and store them directly in XIP. Never populate a runtime flash cache.

### Phase 5 - spend extra SRAM for speed: NOT STARTED

Measure linker/runtime high-water use and XIP-sensitive paths, then enlarge surface/texture/hot-data caches within a documented SRAM budget.

### Phase 6 - RP2350 CPU optimization: NOT STARTED

Only after profiling the MG24 baseline, tune Cortex-M33 hot loops, compiler options, SRAM placement and XIP access patterns with before/after measurements.

## Performance instrumentation

At playable milestones record Core-0 frame time, BSP/edge/surface/span breakdown where practical, renderer counters, XIP/cache proxies where measurable, Core-1 display stalls, audio underruns and peak SRAM. The old 3-5 FPS number measures the temporary reference renderer and is not the MG24 baseline.

## Completion criteria

The migration is complete when the production RP2350 target runs the MG24-derived engine/renderer, uses the validated Core-1 service structure, uses one indexed framebuffer plus two one-row RGB565 DMA buffers, loads immutable resources from 16 MiB XIP without level-change flash writes, retains speed-relevant MG24 optimizations, spends extra SRAM where measurement justifies it, and runs world/entities/collision/audio/input/demo/gameplay with useful profiling counters.
