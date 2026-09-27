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

RP2350 sound assets are converted offline to **QAD1 block IMA ADPCM at 11025 Hz mono**. The measured converted sound set is **1.38 MiB**, saving about **1.34 MiB** relative to the original WAV files.

QAD1 uses **256 decoded samples per independent block**. Each block contains a 16-bit predictor, IMA step index, flags/count fields and packed 4-bit codes. The file header stores decoded sample count, loop start and block size.

The runtime decoder is implemented in `qmix.c`. `qsound_qad1()` validates the complete QAD1 structure before playback, including magic, block size, loop bounds, step index, block counts and file bounds. `qsound_open()` auto-detects QAD1 while retaining the original WAV/PCM parser for diagnostics.

Core 1 decodes QAD1 **incrementally from immutable XIP**. Each mixer channel carries only predictor/index/current decoded position state; there is no 256-sample decode buffer and no whole-sound SRAM copy. Normal sequential playback performs one IMA nibble decode only when the 11025-Hz source advances. Because PWM/mixer output is 22050 Hz, the decoded source sample is naturally held for two output samples. Crossing a QAD1 block invalidates the channel decoder state and initializes from the next independent block header.

Looping or a non-sequential position rebuilds decoder state from the containing independent block and decodes at most 255 nibbles to reach the requested position. This makes arbitrary loop points work without a large seek table or SRAM cache. Normal sequential playback does not repeatedly seek or re-decode a block.

Host tests cover QAD1 validation, incremental decoding, 11025->22050 sample-hold timing, invalid metadata rejection and a loop beginning inside a block. Hardware playback still needs validation with the generated converted asset image once the current resource-image path is connected to the bring-up target.

## Host tools and current Flash status

`Tools/RP2350Pack/mcu_pack_converter.py` performs portable host conversion including alias MDL and QAD1 sound conversion. `Tools/RP2350Pack/xip_image_builder.py` builds QXIP and physically deduplicates exact BSP miptex records into a global texture store.

Current measured shareware footprint after implemented conversions:

- original aligned PAK: **17.43 MiB**;
- Python converted PAK: **15.60 MiB**;
- alias MDL: **2.70 -> 2.20 MiB**;
- sound: **2.72 MiB original WAV -> 1.38 MiB QAD1**;
- global exact BSP texture dedup: **1.15 MiB** saving;
- generated QXIP image: **15,160,036 bytes = 14.46 MiB**;
- firmware reservation: **1.00 MiB**;
- asset budget: **15.00 MiB**;
- measured asset headroom: **about +0.54 MiB**.

No lightmap compression is required for capacity at this stage.

## Migration phases and status

### Phase 0 - validated platform services: COMPLETE/FROZEN

Core-1 display/input/audio scheduling, DMA/ring structure and input structure remain the known-good platform layer. QAD1 decoding was added at the mixer source level without changing the Core-1 service architecture.

### Phase 1 - build the real MG24 engine on RP2350: IN PROGRESS

Goal: compile the real MG24 engine against a thin RP2350 HAL, initialize it, and load `start` from immutable XIP with no level-time flash programming.

Completed checkpoints:

1. RP2350 Phase-1 engine/ABI targets and read-only file/system boundaries exist.
2. Real MG24 model/engine sources are being compiled so converted-format dependencies are exposed directly.
3. Immutable resource placement was corrected to offline conversion plus direct XIP addressing rather than a generic SRAM arena.
4. The Python host converter replaces the host-ABI-dependent C conversion path.
5. Python alias-MDL conversion saves about 0.50 MiB on the shareware set.
6. QXIP global texture extraction produces a real 1.15 MiB texture saving.
7. MG24 packed node/leaf storage was measured and rejected; native BSP nodes/leaves are retained.
8. QAD1 11025-Hz block IMA ADPCM conversion is implemented and measured at about 1.38 MiB for the shareware sound set.
9. **QAD1 runtime decoding is implemented in the Core-1 mixer path**, including block validation, incremental decode, block transitions and arbitrary loop-point seek/rebuild. PCM remains supported for diagnostics.
10. Host mixer tests exercise PCM and QAD1 playback behavior. Hardware QAD1 playback awaits resource-image integration/flash testing.
11. The generated QXIP asset image is **14.46 MiB**, below the **15.00 MiB** asset budget with about **0.54 MiB headroom**.
12. Resource-capacity work is sufficient for Phase 1; lightmap compression is deferred.

Immediate next work:

1. Connect the generated converted/QXIP sound entry path to the bring-up/runtime resource lookup and validate actual shotgun QAD1 playback on RP2350, including `audio_underruns` under simultaneous display activity.
2. Integrate QXIP lookup into model/texture loading: level texture index -> global texture ID -> XIP miptex.
3. Remove RP2350 runtime flash-programming dependencies and load `start` directly through QXIP/XIP.
4. Continue MG24 engine source integration and profile SRAM/XIP hot paths.
5. Measure final linked firmware size against the 1.00 MiB reservation.

Phase 1 is not complete until the real engine links and `start` loads through the production XIP path.

### Phase 2 - restore MG24 world rendering: NOT STARTED

Bring up BSP/PVS -> edge -> surface -> span rendering into the single indexed framebuffer and hand completed frames to unchanged Core 1.

### Phase 3 - model/collision/game integration: NOT STARTED

Restore entities, alias models, sprites, particles, sky/turbulence, dynamic lighting, collision and gameplay using the MG24-derived paths.

### Phase 4 - proven host-side hot formats: IN PROGRESS

Alias MDL conversion, QXIP global texture dedup and QAD1 block IMA ADPCM are host-side transformations selected from measured RP2350 benefit. MG24 node/leaf packing is explicitly excluded. QAD1 now has both host encoder and runtime incremental decoder.

### Phase 5 - spend extra SRAM for speed: NOT STARTED

Measure linker/runtime high-water use and XIP-sensitive paths, then enlarge caches/working sets within a documented budget.

### Phase 6 - RP2350 CPU optimization: NOT STARTED

Profile the MG24 baseline first, then tune Cortex-M33 loops, compiler options, SRAM placement and XIP access with before/after measurements.

## Completion criteria

Production must run the MG24-derived engine/renderer, retain the validated Core-1 service structure, use one indexed framebuffer plus two one-row RGB565 buffers, directly address runtime-ready immutable resources from 16 MiB XIP without level-change flash writes, use QAD1 sounds directly from XIP with incremental Core-1 decoding, keep native BSP nodes/leaves, and spend SRAM only on mutable/working/measured-hot data.
