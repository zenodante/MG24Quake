# RP2350 Quake port plan

Reference: [next-hack, original MG24 Quake port, 2024-09-22](https://next-hack.com/index.php/2024/09/22/quake-port-to-sparkfun-and-arduino-nano-matter-boards-using-only-276-kb-ram/).

Central rule: **MG24 is the engine/renderer baseline, not the RP2350 hardware baseline.** Keep the author's Quake algorithm and performance work; replace hardware and memory compromises that existed only because MG24 had 276 kB RAM and external SPI storage.

## Fixed RP2350 architecture

Core 0 runs Quake/MG24. One 320x200 8-bit indexed framebuffer is shared through an ownership barrier. Core 1 owns the already validated framebuffer-to-RGB565 conversion, two independent 320-pixel one-row ping-pong buffers, LCD DMA, input polling and audio mixing/output. Do not reintroduce a second full framebuffer or multi-row staging buffers. qservice/qmix and the board drivers are frozen unless a measured integration defect requires a change.

## Renderer decision

`qrender.c` is diagnostic only. Production uses MG24 `r_main/r_bsp/r_edge/r_surf/r_sky`, `d_edge/d_scan/d_surf` and the related sprite/alias paths. Preserve edge/surface/span rendering, fixed point, reduced division and valid Cortex-M packed operations, especially the optimized `D_DrawSpans8` family. `qbsp/qcollision/qrender` leave the production build as their MG24 equivalents become functional.

## RP2350 memory/storage policy

Use the 520 kB SRAM as a performance resource and the 16 MiB memory-mapped flash as an immutable bulk-resource store. Preferred access is direct contiguous XIP pointer, then bounded XIP reader, then SRAM copy/cache for hot or mutable state, then host-derived representation when it removes measured runtime work. Never populate a level-time flash cache.

The MG24 `extMemory` abstraction is retained at the source boundary where useful, but on RP2350 it means memory-mapped XIP, not an emulated SPI device. EFR32 asynchronous external-flash DMA must not compete with the validated Core-1 DMA design.

## Host tools

`Tools/MCUPackConverter` mixes speed transformations with MG24 storage workarounds. Audit each dependency individually. `Tools/RP2350Pack/pack.py` remains useful for diagnostics but compressed pages are not the default hot-resource representation. `Tools/RP2350Pack/prepare_xip.py` is the uncompressed production staging baseline: merge pak0/pak1, later PAK wins, align data, no runtime flash writes.

Classify converted-format dependencies as speed transformation (retain host-side), RAM workaround (normally remove/use SRAM), external-flash workaround (remove), or format convenience (choose by measured cost).

## Migration phases and status

### Phase 0 - validated platform services: COMPLETE/FROZEN

Core-1 display/input/audio structure is the known-good platform layer.

### Phase 1 - build the real MG24 engine on RP2350: IN PROGRESS

Goal: compile the real MG24 engine against a thin RP2350 HAL, initialize it, and load `start` from immutable XIP with no level-time flash programming.

Implemented checkpoints:

1. `quake_rp2350_engine_phase1` exists beside the old diagnostic executable and includes the real `QuakeMG24/Quake` headers.
2. `qengine_probe.c` checks the MG24 engine ABI/data model instead of introducing a new renderer/data model.
3. `qfiles.c` supplies the Quake read-only `Sys_FileOpenRead/Close/Seek/Read/Time` API over the RP2350 resource image; write APIs remain disabled intentionally.
4. `qsys.c` now supplies the first non-file RP2350 system HAL: `Sys_FloatTime`, print/error/quit, console/sleep and floating-point-control no-ops. Input events are deliberately left as a qservice bridge rather than importing MG24 input hardware.
5. `platform/rp2350/extMemory.h` now shadows the EFR32 `QuakeMG24/src/extMemory.h` for the Phase-1 target. MG24 external-memory reads become direct XIP dereference/memcpy. The old SPI command latency, interleaved-flash DMA, EUSART restoration and flash-program/erase operations are absent. Asynchronous-read entry points currently complete synchronously because XIP is memory mapped; this preserves source compatibility without importing a fake second DMA architecture.
6. `model.c` has been added to the Phase-1 compile target. This is intentional: its failures now expose the real remaining model-loader dependencies rather than allowing the diagnostic qbsp loader to hide them.
7. The `model.c` audit identifies the main migration split. `Mod_LoadModel` currently obtains assets through `getExtMemPointerToFileInPak`; the alias-memory-ready loader then repeatedly calls `storeToInternalFlash`, `reserveInternalFlashSize`, `getCurrentInternalFlashPtr` and `storeToInternalFlashAtPointer`. These are MG24 level-cache/RAM-pressure mechanisms and are not acceptable RP2350 runtime flash writes. The useful converted alias layout and renderer-facing compact structures must be separated from that storage policy.
8. Core-1 code remains untouched.

Current model-loader decision:

- Preserve the MG24 converted alias representation when it removes runtime triangle/model work.
- Replace `getExtMemPointerToFileInPak` with a resource lookup that returns an immutable contiguous XIP pointer plus size when possible.
- Replace `storeToInternalFlash` family calls with a level/model arena in SRAM for mutable/relocated metadata, or direct XIP offsets/pointers for immutable converted arrays whose representation is already runtime-ready.
- Do not simply change `storeToInternalFlash()` into `malloc()`: allocation lifetime is level/model scoped and must be explicit so SRAM high-water use is measurable and resettable.
- Do not restore EFR32 external-memory DMA; XIP access is the baseline and SRAM caching is introduced only for measured hot data.

Next Phase-1 work:

1. Add an RP2350 level/model arena and compatibility allocation boundary for the MG24 loader, with reset/high-water accounting.
2. Add a direct qfiles/qpak lookup returning immutable XIP pointer + size, and adapt the MG24 PAK lookup boundary to it.
3. Compile `model.c` through the remaining converted-format dependencies and remove every runtime flash-programming dependency from its RP2350 path.
4. Add additional non-render engine source groups and inventory failures as EFR32 hardware, converted-format, flash-cache, or normal HAL dependencies.
5. Audit compact generated QuakeC/entity pointer/index assumptions against RP2350 address width and arena placement.
6. Add the production entry point only after the engine object set compiles cleanly. First runtime milestone remains engine initialization + `start` load through `model.c`.

Phase 1 is **not complete** until the real engine source set links and `start` is loaded through the MG24 model path. The diagnostic BSP loader does not count.

### Phase 2 - restore MG24 world rendering: NOT STARTED

Bring up BSP/PVS -> edge -> surface -> span rendering into the single indexed framebuffer and hand completed frames to unchanged Core 1. Measure Core-0 render time separately from display transfer.

### Phase 3 - model/collision/game integration: NOT STARTED

Use MG24/Quake model/hull and collision paths; retire diagnostic qbsp/qcollision as replacements become functional; restore entities, alias models, sprites, particles, sky/turbulence, dynamic lighting and gameplay.

### Phase 4 - proven host-side hot formats: NOT STARTED

Port only speed-relevant MCUPackConverter transformations into RP2350 host tooling; generated bytes live directly in XIP.

### Phase 5 - spend extra SRAM for speed: NOT STARTED

Measure linker/runtime high-water use and XIP-sensitive paths, then enlarge caches/working sets within a documented budget.

### Phase 6 - RP2350 CPU optimization: NOT STARTED

Profile the MG24 baseline first, then tune Cortex-M33 loops, compiler options, SRAM placement and XIP access with before/after measurements.

## Performance instrumentation

At playable milestones record Core-0 frame time and renderer breakdown/counters, Core-1 stalls, audio underruns, XIP-sensitive metrics where practical, and SRAM current/peak/high-water usage. The old 3-5 FPS result is only the temporary reference renderer measurement.

## Completion criteria

Production must run the MG24-derived engine/renderer, retain the validated Core-1 service structure, use one indexed framebuffer plus two one-row RGB565 buffers, load immutable resources from 16 MiB XIP without level-change flash writes, preserve speed-relevant MG24 optimizations, spend additional SRAM where measurement justifies it, and run world/entities/collision/audio/input/demo/gameplay with profiling sufficient for further optimization.
