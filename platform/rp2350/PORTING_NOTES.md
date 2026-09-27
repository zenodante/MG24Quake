# RP2350 Quake port plan

Reference: [next-hack, original MG24 Quake port, 2024-09-22](https://next-hack.com/index.php/2024/09/22/quake-port-to-sparkfun-and-arduino-nano-matter-boards-using-only-276-kB-ram/).

Central rule: **MG24 is the engine/renderer baseline, not the RP2350 hardware baseline.** Keep the author's Quake algorithm and performance work; replace hardware and memory compromises that existed only because MG24 had 276 kB RAM and external SPI storage.

## Fixed RP2350 architecture

Core 0 runs Quake/MG24. One 320x200 8-bit indexed framebuffer is shared through an ownership barrier. Core 1 owns the validated framebuffer-to-RGB565 conversion, two independent 320-pixel one-row ping-pong buffers, LCD DMA, input polling and audio mixing/output. Do not reintroduce a second full framebuffer or multi-row staging buffers. qservice and board drivers remain structurally unchanged unless measured integration exposes a defect; qmix now additionally decodes the selected QAD1 source format.

## Renderer decision

Production uses the MG24 renderer paths, not the diagnostic qrender implementation. Preserve the original port's edge/surface/span rendering and measured CPU optimizations unless RP2350 profiling supports a change.

## RP2350 memory/storage policy

The production resource model is **offline conversion -> immutable runtime-ready XIP image -> direct addressing**. SRAM is reserved for mutable/runtime-generated state, engine working sets, and immutable hot subsets only when profiling justifies a copy.

The 16 MiB flash must never be repopulated at level change. Useful MG24 format conversion moves to host tools; MG24 storage-placement work is discarded. On RP2350, external-memory style reads mean memory-mapped XIP.

**RP2350 does not use the MG24 packed/combined node+leaf storage format.** Analysis showed approximately +0.03 MiB across the shareware BSP set rather than a saving. Native BSP nodes and leaves therefore remain in XIP unless profiling later motivates an RP2350-specific representation.

## Sound storage and playback

Shareware `pak0` contains **190 WAV sounds**, totaling about **2.72 MiB** and **251 seconds**. 188 are 11025 Hz / 8-bit and two are 22050 Hz / 16-bit.

RP2350 sound assets are converted offline to **QAD1 block IMA ADPCM at 11025 Hz mono**. The measured converted sound set is **1.38 MiB**, saving about **1.33 MiB (49.1%)** relative to the original WAV files.

QAD1 uses **256 decoded samples per independent block**. Each block contains a 16-bit predictor, IMA step index, flags/count fields and packed 4-bit codes. The file header stores decoded sample count, loop start and block size.

The runtime decoder is implemented in `qmix.c`. `qsound_qad1()` validates the complete QAD1 structure before playback, including magic, block size, loop bounds, step index, block counts and file bounds. `qsound_open()` auto-detects QAD1 while retaining the original WAV/PCM parser for diagnostics.

Core 1 decodes QAD1 **incrementally from immutable XIP**. Each mixer channel carries only predictor/index/current decoded position state; there is no 256-sample decode buffer and no whole-sound SRAM copy. Normal sequential playback performs one IMA nibble decode only when the 11025-Hz source advances. Because PWM/mixer output is 22050 Hz, the decoded source sample is naturally held for two output samples. Crossing a QAD1 block invalidates the channel decoder state and initializes from the next independent block header.

Looping or a non-sequential position rebuilds decoder state from the containing independent block and decodes at most 255 nibbles to reach the requested position. This makes arbitrary loop points work without a large seek table or SRAM cache. Normal sequential playback does not repeatedly seek or re-decode a block.

Host tests cover QAD1 validation, incremental decoding, 11025->22050 sample-hold timing, invalid metadata rejection and a loop beginning inside a block. **Hardware playback has now been validated:** the converted shotgun sound plays correctly from the combined QXIP image through XIP -> QAD1 decoder -> Core-1 mixer -> DMA/PWM -> speaker.

## Host tools and current Flash status

`Tools/RP2350Pack/mcu_pack_converter.py` performs portable host conversion including alias MDL and QAD1 sound conversion. `Tools/RP2350Pack/xip_image_builder.py` builds QXIP and physically deduplicates exact BSP miptex records into a global texture store. `Tools/RP2350Pack/run_pipeline.py` is the preferred one-command conversion entry point so MDL/QAD1 conversion is not accidentally bypassed. `Tools/RP2350Pack/make_uf2.py` validates QXIP1 and can produce a combined firmware+asset UF2. `Tools/RP2350Pack/make_asset_uf2.py` produces an **asset-only UF2** beginning at the 1 MiB asset partition, allowing firmware and immutable resources to be flashed independently during development.

Current measured shareware footprint after implemented conversions:

- original aligned PAK: **17.43 MiB**;
- Python converted PAK: **15.60 MiB**;
- alias MDL: **2.70 -> 2.20 MiB**;
- sound: **2.72 MiB original WAV -> 1.38 MiB QAD1**;
- global exact BSP texture dedup: **1.15 MiB** saving;
- generated BSP-compatible QXIP image: **15,161,212 bytes = 14.46 MiB**;
- firmware reservation: **1.00 MiB**;
- asset budget: **15.00 MiB**;
- measured asset headroom: **about +0.54 MiB**.

No lightmap compression is required for capacity at this stage.

## Reproducible firmware + QXIP build

The preferred development workflow keeps firmware and immutable resources as separate UF2 images. The examples below assume the original shareware PAK is `build/pak0.pak` and the RP2350 build directory is `build_rp2350`.

### 1. Build firmware

After CMake has been configured with the desired local Pico SDK/toolchain paths:

```sh
cmake --build build_rp2350 -j
```

This produces the firmware-only files including:

```text
build_rp2350/quake_rp2350_bringup.uf2
build_rp2350/quake_rp2350_bringup.bin
```

The firmware UF2 writes only the reserved first 1 MiB firmware region. During engine development this is the image that should normally be rebuilt and reflashed repeatedly.

### 2. Regenerate converted QXIP assets

Always use the complete Python pipeline rather than invoking `xip_image_builder.py` directly on the original PAK. This ensures alias MDL conversion, QAD1 sound conversion, BSP-compatible level generation and global texture dedup are all applied:

```sh
python3 Tools/RP2350Pack/run_pipeline.py \
  build/pak0.pak \
  -o build_rp2350 \
  --firmware-bytes 0x100000
```

The runtime asset image is:

```text
build_rp2350/quake-assets.qxip
```

For the current shareware set the expected report is approximately 14.46 MiB QXIP, 15.00 MiB asset budget and +0.54 MiB headroom. The current measured image is 15,161,212 bytes with 339 files, 21 levels, 396 unique global textures and zero sound-conversion errors.

### 3. Generate the independent asset UF2

```sh
python3 Tools/RP2350Pack/make_asset_uf2.py \
  build_rp2350/quake-assets.qxip \
  build_rp2350/quake-assets.uf2
```

`quake-assets.uf2` contains **assets only**. Its first payload byte is written at the asset partition start, normally flash/XIP address `0x10100000`, corresponding to the **1 MiB offset** after firmware. The script validates QXIP1, the TEX1 store and the save-partition boundary. It does not write the firmware partition and does not write the persistent save partition.

Flash this asset image whenever the converted resources or QXIP layout change:

```text
build_rp2350/quake-assets.uf2
```

After the current asset image has been flashed once, ordinary engine-code iterations require only rebuilding and flashing:

```text
build_rp2350/quake_rp2350_bringup.uf2
```

This avoids rewriting approximately 14.46 MiB of immutable resources for every firmware change.

### 4. Optional combined UF2

A single complete image remains useful for provisioning a blank board or when both firmware and assets changed:

```sh
python3 Tools/RP2350Pack/make_uf2.py \
  build_rp2350/quake_rp2350_bringup.uf2 \
  build_rp2350/quake-assets.qxip \
  build_rp2350/quake_rp2350_full.uf2
```

The combined image writes firmware plus the immutable asset partition while leaving the save partition untouched. It is no longer the preferred image for routine firmware-only development.

## Migration phases and status

### Phase 0 - validated platform services: COMPLETE/FROZEN

Core-1 display/input/audio scheduling, DMA/ring structure and input structure remain the known-good platform layer. QAD1 decoding was added at the mixer source level without changing the Core-1 service architecture.

### Phase 1 - build the real MG24 engine on RP2350: IN PROGRESS

Goal: compile the real MG24 engine against a thin RP2350 HAL, initialize it, and load `start` from immutable XIP with no level-time flash programming.

Completed checkpoints:

1. RP2350 Phase-1 engine/ABI targets and read-only file/system boundaries exist.
2. Real MG24 `model.c` now compiles successfully for the RP2350 Phase-1 engine target through a forced `qengine_compat.h` compatibility layer; the engine no longer needs Silicon Labs generated headers merely to compile.
3. Immutable resource placement was corrected to offline conversion plus direct XIP addressing rather than a generic SRAM arena.
4. The Python host converter replaces the host-ABI-dependent C conversion path.
5. Python alias-MDL conversion saves about 0.50 MiB on the shareware set.
6. QXIP global texture extraction produces a real 1.15 MiB texture saving.
7. MG24 packed node/leaf storage was measured and rejected; native BSP nodes/leaves are retained.
8. QAD1 11025-Hz block IMA ADPCM conversion is implemented and measured at about 1.38 MiB for the shareware sound set.
9. **QAD1 runtime decoding is implemented and hardware validated in the Core-1 mixer path**, including block validation, incremental decode, block transitions and arbitrary loop-point seek/rebuild. PCM remains supported for diagnostics.
10. QXIP1 is connected to the runtime asset/file interface. Ordinary files are mapped directly from XIP, and actual shotgun playback from converted QXIP/QAD1 assets works on RP2350 hardware.
11. QXIP exposes zero-copy level lump and global TEX1 lookup. The host builder now emits BSP-compatible level texture directories whose relative offsets can point directly into the shared global TEX1 store, allowing the original MG24 texture pointer arithmetic to remain usable without SRAM texture copies.
12. The regenerated BSP-compatible QXIP asset image is **15,161,212 bytes (14.46 MiB)**, below the **15.00 MiB** asset budget with about **0.54 MiB headroom**.
13. Resource-capacity work is sufficient for Phase 1; lightmap compression is deferred.
14. The real MG24 model file ABI now resolves files through the RP2350 QXIP mapping layer, so `Mod_LoadModel()` can receive a direct XIP pointer rather than an SRAM/file-cache copy.
15. Firmware and immutable assets can now be flashed independently: `quake-assets.uf2` starts at the 1 MiB asset partition, while routine firmware iterations update only the firmware UF2.

Immediate next work:

1. Turn the current Phase-1 compile probe into a **runtime model-loader probe** that mounts QXIP, calls `Mod_Init()` and loads `maps/start.bsp` through `Mod_ForName()` / `Mod_LoadModel()` / `Mod_LoadBrushModel()`.
2. Add narrow serial checkpoints around the real brush-model loader so the first remaining MG24 platform/storage assumption can be identified on hardware rather than guessed in advance.
3. Validate that the original MG24 `Mod_LoadTextures()` follows the generated cross-QXIP relative `dataofs[]` directly into TEX1 and that all bounds/pointer assumptions remain valid on RP2350.
4. Remove any remaining runtime flash-programming dependency actually encountered on the real loading path; do not pre-emptively rewrite engine logic that is already portable.
5. Continue MG24 engine source integration and profile SRAM/XIP hot paths.
6. Measure final linked firmware size against the 1.00 MiB reservation.

Phase 1 is not complete until the real engine links and `start` loads through the production XIP path.

### Phase 2 - restore MG24 world rendering: NOT STARTED

Bring up BSP/PVS -> edge -> surface -> span rendering into the single indexed framebuffer and hand completed frames to unchanged Core 1.

### Phase 3 - model/collision/game integration: NOT STARTED

Restore entities, alias models, sprites, particles, sky/turbulence, dynamic lighting, collision and gameplay using the MG24-derived paths.

### Phase 4 - proven host-side hot formats: IN PROGRESS

Alias MDL conversion, QXIP global texture dedup and QAD1 block IMA ADPCM are host-side transformations selected from measured RP2350 benefit. MG24 node/leaf packing is explicitly excluded. QAD1 now has both host encoder and runtime incremental decoder with successful hardware playback from QXIP.

### Phase 5 - spend extra SRAM for speed: NOT STARTED

Measure linker/runtime high-water use and XIP-sensitive paths, then enlarge caches/working sets within a documented budget.

### Phase 6 - RP2350 CPU optimization: NOT STARTED

Profile the MG24 baseline first, then tune Cortex-M33 loops, compiler options, SRAM placement and XIP access with before/after measurements.

## Completion criteria

Production must run the MG24-derived engine/renderer, retain the validated Core-1 service structure, use one indexed framebuffer plus two one-row RGB565 buffers, directly address runtime-ready immutable resources from 16 MiB XIP without level-change flash writes, use QAD1 sounds directly from XIP with incremental Core-1 decoding, keep native BSP nodes/leaves, and spend SRAM only on mutable/working/measured-hot data.
