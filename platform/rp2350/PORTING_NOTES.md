# RP2350 Quake port plan

Reference: [next-hack, original MG24 Quake port, 2024-09-22](https://next-hack.com/index.php/2024/09/22/quake-port-to-sparkfun-and-arduino-nano-matter-boards-using-only-276-kb-ram/).

Central rule: **MG24 is the engine/renderer baseline, not the RP2350 hardware baseline.** Keep the author's Quake algorithm and performance work; replace hardware and memory compromises that existed only because MG24 had 276 kB RAM and external SPI storage.

## Fixed RP2350 architecture

Core 0 runs Quake/MG24. One 320x200 8-bit indexed framebuffer is shared through an ownership barrier. Core 1 owns the validated framebuffer-to-RGB565 conversion, two independent 320-pixel one-row ping-pong buffers, LCD DMA, input polling and audio mixing/output. Do not reintroduce a second full framebuffer or multi-row staging buffers. qservice/qmix and board drivers are frozen unless measured integration exposes a defect.

## Renderer decision

`qrender.c` is diagnostic only. Production uses MG24 `r_main/r_bsp/r_edge/r_surf/r_sky`, `d_edge/d_scan/d_surf` and related sprite/alias paths. Preserve edge/surface/span rendering, fixed point, reduced division and valid Cortex-M packed operations. `qbsp/qcollision/qrender` leave production as MG24 equivalents become functional.

## RP2350 memory/storage policy

The production resource model is **offline conversion -> immutable runtime-ready XIP image -> direct addressing**. SRAM is not a generic replacement for MG24 internal flash.

A resource belongs in SRAM only when it is mutable/runtime-generated, is a renderer/engine working set, or profiling proves that its XIP access pattern materially costs performance. Immutable BSP/model/texture/light/sound data starts in XIP even when it is frequently read. Hot immutable subsets may later be promoted to SRAM based on measurement.

The 16 MiB flash must never be repopulated at level change. MG24 runtime `storeToInternalFlash` work is split into (a) useful format conversion, which moves to the PC tool when appropriate, and (b) storage placement, which is discarded. The MG24 external-memory abstraction on RP2350 means memory-mapped XIP, not emulated SPI/DMA.

**RP2350 does not use the MG24 packed/combined node+leaf storage format.** The Python conversion analysis measured the MG24 node/leaf transformation as approximately **+0.03 MiB** across the shareware BSP set rather than a Flash saving. That representation solved MG24-specific memory/storage constraints and is not beneficial here. RP2350 therefore keeps the native BSP node and leaf representation in XIP unless later profiling demonstrates a performance reason for a different RP2350-specific layout. Do not implement the MG24 node/leaf serializer in the RP2350 asset pipeline.

## Host tools

`Tools/RP2350Pack/build_assets.py` is the resource-analysis gate. `Tools/RP2350Pack/mcu_pack_converter.py` provides portable Python host conversion for transformations that are useful on RP2350, including the alias-MDL and WAV conversions. `Tools/RP2350Pack/xip_image_builder.py` builds the RP2350-specific QXIP image and physically deduplicates identical BSP miptex records into a global texture store.

The current shareware measurements are:

- original aligned PAK image: **17.43 MiB**;
- Python converted image before global texture extraction: **16.86 MiB**;
- alias MDL data: **2.70 -> 2.20 MiB**, saving about **0.50 MiB**;
- real global texture saving: about **1.15 MiB**;
- actual generated QXIP asset image: **15.71 MiB**;
- with a conservative 1.50 MiB firmware reservation, asset budget is **14.50 MiB**, leaving a current real deficit of about **1.21 MiB**;
- applying the MG24 node/leaf transformation would increase the projected image by about **0.03 MiB**, so it is explicitly rejected for RP2350.

QXIP texture identity is based on complete miptex content rather than texture name. This is required because the shareware data contains same-name textures with different contents. Each level stores global texture IDs while each unique miptex record is stored once in XIP.

The remaining Flash optimization work should target resources that produce a measured net saving on RP2350. Do not copy an MG24 format merely because it exists in the original port.

## Migration phases and status

### Phase 0 - validated platform services: COMPLETE/FROZEN

Core-1 display/input/audio structure is the known-good platform layer.

### Phase 1 - build the real MG24 engine on RP2350: IN PROGRESS

Goal: compile the real MG24 engine against a thin RP2350 HAL, initialize it, and load `start` from immutable XIP with no level-time flash programming.

Completed checkpoints:

1. `quake_rp2350_engine_phase1` and the MG24 ABI probe exist beside the old diagnostic target.
2. `qfiles.c` supplies read-only Quake file APIs; `qsys.c` supplies the initial RP2350 system boundary.
3. RP2350 `extMemory.h` maps reads to XIP/memcpy and imports none of the EFR32 interleaved-SPI/DMA/programming architecture.
4. Real MG24 `model.c` is in the Phase-1 compile target so converted-format/storage dependencies are exposed directly.
5. The resource strategy was corrected: do not build a general SRAM model arena as a replacement for `storeToInternalFlash`; immutable resources are prepared offline and directly addressed from XIP.
6. `build_assets.py` provides Flash/SRAM analysis and per-BSP lump/texture accounting.
7. The host C `MCUPackConverter` path was replaced for RP2350 asset preparation by a portable Python converter, avoiding 32-bit-MCU versus 64-bit-host ABI dependence.
8. The Python alias-MDL converter is active and reduces the shareware MDL set from about 2.70 MiB to 2.20 MiB.
9. `xip_image_builder.py` generates a real QXIP image with a global content-addressed BSP texture store. The measured image is about 15.71 MiB and the real texture saving is about 1.15 MiB.
10. **Decision recorded: do not use the MG24 combined/packed node+leaf representation.** Analysis projects approximately +0.03 MiB rather than a saving. Native BSP nodes/leaves remain separate XIP data on RP2350.
11. Core-1 code remains untouched.

Immediate next work:

1. Reduce the remaining measured QXIP Flash deficit of about 1.21 MiB. Analyze the major remaining resource classes before choosing compression or another representation; BSP lighting is currently a major candidate because the original shareware lighting lumps total about 1.35 MiB.
2. Integrate QXIP lookup into the RP2350 model/texture loading path: level texture index -> global texture ID -> directly addressed XIP miptex.
3. Remove the obsolete MG24 node/leaf size projection from decisions and tools where it could be mistaken for a planned RP2350 format. Keep it only as historical/comparison data if useful.
4. Audit the remaining `MCUPackConverter` transformations against their engine consumers and port only transformations with a measured RP2350 benefit.
5. Keep only mutable state and measured hot working sets in SRAM; add their real sizes to the report as the engine links.
6. Remove runtime flash-programming dependencies from the RP2350 `model.c` path and load `start` through QXIP/XIP.
7. Add further non-render engine source groups and inventory EFR32/converted-format/HAL dependencies.
8. Add the production entry point after the engine object set compiles cleanly.

Phase 1 is **not complete** until the real engine source set links and `start` is loaded through the production RP2350 XIP model path. Diagnostic qbsp loading does not count.

### Phase 2 - restore MG24 world rendering: NOT STARTED

Bring up BSP/PVS -> edge -> surface -> span rendering into the single indexed framebuffer and hand completed frames to unchanged Core 1. Measure Core-0 render time separately from display transfer.

### Phase 3 - model/collision/game integration: NOT STARTED

Use MG24/Quake model/hull and collision paths; retire diagnostic qbsp/qcollision; restore entities, alias models, sprites, particles, sky/turbulence, dynamic lighting and gameplay.

### Phase 4 - proven host-side hot formats: STARTED EARLY AS PHASE-1 DEPENDENCY

The Python converter and QXIP builder exist. Port only speed/space-relevant MG24 transformations after consumer audit; generated runtime-ready bytes live directly in XIP. The MG24 node/leaf representation has been evaluated and rejected for RP2350 because it increases the projected Flash footprint.

### Phase 5 - spend extra SRAM for speed: NOT STARTED

Measure linker/runtime high-water use and XIP-sensitive paths, then enlarge caches/working sets within a documented budget.

### Phase 6 - RP2350 CPU optimization: NOT STARTED

Profile the MG24 baseline first, then tune Cortex-M33 loops, compiler options, SRAM placement and XIP access with before/after measurements.

## Performance instrumentation

At playable milestones record Core-0 frame time and renderer breakdown/counters, Core-1 stalls, audio underruns, XIP-sensitive metrics where practical, and SRAM current/peak/high-water usage. The old 3-5 FPS result is only the temporary reference renderer measurement.

## Completion criteria

Production must run the MG24-derived engine/renderer, retain the validated Core-1 service structure, use one indexed framebuffer plus two one-row RGB565 buffers, directly address runtime-ready immutable resources from 16 MiB XIP without level-change flash writes, preserve only MG24 transformations that are beneficial on RP2350, keep native BSP nodes/leaves unless profiling justifies an RP2350-specific alternative, spend SRAM only on mutable/working/measured-hot data, and run world/entities/collision/audio/input/demo/gameplay with profiling sufficient for further optimization.
