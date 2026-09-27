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

## Host tools

`Tools/RP2350Pack/build_assets.py` is now the resource-planning gate for Phase 1. It:

- merges pak0 plus optional pak1 using later-PAK override semantics;
- writes an uncompressed 4-byte-aligned immutable XIP PAK;
- inventories every asset and category;
- parses Quake BSP29 and reports all 15 lumps, offsets, sizes, known record counts/strides, and an initial placement policy;
- parses basic alias MDL geometry counts;
- evaluates the complete XIP image against physical Flash minus a configurable firmware reservation;
- reports known mandatory SRAM (single 320x200 framebuffer and two one-row RGB565 buffers) separately from still-unbudgeted engine/renderer/audio/stack working memory;
- classifies immutable BSP traversal/render lumps as `profile`: direct XIP first, SRAM promotion only if profiling justifies it;
- emits a JSON manifest for later converter/runtime integration.

Example:

```
python3 Tools/RP2350Pack/build_assets.py pak0.pak pak1.pak \
  -o build/quake_xip.pak --json build/quake_assets.json \
  --firmware-bytes 0x180000
```

A non-fitting image exits nonzero. `prepare_xip.py` remains a simpler staging utility; `pack.py` remains useful for diagnostics. `MCUPackConverter` is the source to audit for speed-relevant transformations, not a storage architecture to copy.

The current builder intentionally does **not** invent a new BSP/MDL ABI yet. Each MG24 conversion is added only after its engine consumer is audited, then the generated runtime-ready representation and its XIP/SRAM policy become explicit in the manifest.

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
5. The resource strategy was corrected: do not build a general SRAM model arena as a replacement for `storeToInternalFlash`. First generate/measure the final XIP resource layout.
6. `build_assets.py` now provides that Flash/SRAM analysis gate and a machine-readable manifest.
7. Core-1 code remains untouched.

Immediate next work:

1. Run `build_assets.py` against the actual target PAK set and record real 16 MiB fit/headroom and per-level lump sizes. This measurement decides whether any compression/deduplication is needed.
2. Audit `MCUPackConverter` against `model.c` and renderer consumers. For each `storeToInternalFlash` result, determine whether the converted bytes can be emitted directly by `build_assets.py` and addressed in place from XIP.
3. Extend the image/manifest with those proven runtime-ready representations. Prefer 32-bit image-relative offsets where host-generated absolute pointers would otherwise require relocation.
4. Keep only mutable state and measured hot working sets in SRAM; add their real sizes to the report as the engine links.
5. Remove runtime flash-programming dependencies from the RP2350 `model.c` path and load `start` through that path.
6. Add further non-render engine source groups and inventory EFR32/converted-format/HAL dependencies.
7. Add the production entry point after the engine object set compiles cleanly.

Phase 1 is **not complete** until the real engine source set links and `start` is loaded through the MG24 model path. Diagnostic qbsp loading does not count.

### Phase 2 - restore MG24 world rendering: NOT STARTED

Bring up BSP/PVS -> edge -> surface -> span rendering into the single indexed framebuffer and hand completed frames to unchanged Core 1. Measure Core-0 render time separately from display transfer.

### Phase 3 - model/collision/game integration: NOT STARTED

Use MG24/Quake model/hull and collision paths; retire diagnostic qbsp/qcollision; restore entities, alias models, sprites, particles, sky/turbulence, dynamic lighting and gameplay.

### Phase 4 - proven host-side hot formats: STARTED EARLY AS PHASE-1 DEPENDENCY

The asset builder/manifest exists. Port only speed-relevant MCUPackConverter transformations after consumer audit; generated runtime-ready bytes live directly in XIP.

### Phase 5 - spend extra SRAM for speed: NOT STARTED

Measure linker/runtime high-water use and XIP-sensitive paths, then enlarge caches/working sets within a documented budget.

### Phase 6 - RP2350 CPU optimization: NOT STARTED

Profile the MG24 baseline first, then tune Cortex-M33 loops, compiler options, SRAM placement and XIP access with before/after measurements.

## Performance instrumentation

At playable milestones record Core-0 frame time and renderer breakdown/counters, Core-1 stalls, audio underruns, XIP-sensitive metrics where practical, and SRAM current/peak/high-water usage. The old 3-5 FPS result is only the temporary reference renderer measurement.

## Completion criteria

Production must run the MG24-derived engine/renderer, retain the validated Core-1 service structure, use one indexed framebuffer plus two one-row RGB565 buffers, directly address runtime-ready immutable resources from 16 MiB XIP without level-change flash writes, preserve speed-relevant MG24 transformations, spend SRAM only on mutable/working/measured-hot data, and run world/entities/collision/audio/input/demo/gameplay with profiling sufficient for further optimization.
