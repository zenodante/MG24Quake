# RP2350 Quake port plan

Reference: [next-hack, original MG24 Quake port, 2024-09-22](https://next-hack.com/index.php/2024/09/22/quake-port-to-sparkfun-and-arduino-nano-matter-boards-using-only-276-kb-ram/).

Central rule: **MG24 is the engine/renderer baseline, not the RP2350 hardware baseline.** Keep the author's Quake algorithm and performance work; replace hardware and memory compromises that existed only because MG24 had 276 kB RAM and external SPI storage.

## Fixed RP2350 architecture

Core 0 runs Quake/MG24. One 320x200 8-bit indexed framebuffer is shared through an ownership barrier. Core 1 owns the validated framebuffer-to-RGB565 conversion, two independent 320-pixel one-row ping-pong buffers, LCD DMA, input polling and audio mixing/output. Do not reintroduce a second full framebuffer or multi-row staging buffers. qservice/qmix and board drivers are frozen unless measured integration exposes a defect.

## Renderer decision

Production uses the MG24 renderer paths, not the diagnostic qrender implementation. Preserve the original port's edge/surface/span rendering and measured CPU optimizations unless RP2350 profiling supports a change.

## RP2350 memory/storage policy

The production resource model is **offline conversion -> immutable runtime-ready XIP image -> direct addressing**. SRAM is reserved for mutable/runtime-generated state, engine working sets, and immutable hot subsets only when profiling justifies a copy.

The 16 MiB flash must never be repopulated at level change. Useful MG24 format conversion moves to host tools; MG24 storage-placement work is discarded. On RP2350, external-memory style reads mean memory-mapped XIP.

**RP2350 does not use the MG24 packed/combined node+leaf storage format.** Analysis showed approximately +0.03 MiB across the shareware BSP set rather than a saving. Native BSP nodes and leaves therefore remain in XIP unless profiling later motivates an RP2350-specific representation.

## Sound storage and playback

Shareware `pak0` contains **190 WAV sounds**, totaling about **2.72 MiB** and **251 seconds**. 188 are 11025 Hz / 8-bit and two are 22050 Hz / 16-bit.

RP2350 sound assets are converted offline to **QAD1 block IMA ADPCM at 11025 Hz mono**. The sample rate is deliberately retained rather than dropping to 5512.5 or 8000 Hz. The measured converted sound set is **1.38 MiB**, saving about **1.34 MiB** relative to the original 2.72 MiB WAV files while retaining 11025-Hz temporal resolution.

The QAD1 format uses **256 decoded samples per independent block**. Each block carries its own 16-bit predictor, IMA step index and decoded-sample count, followed by packed 4-bit IMA codes. The sound header records total decoded samples, loop start and block size. Independent blocks bound seek cost and allow Quake's mixer to begin/continue near an arbitrary sample position without decoding an entire sound into SRAM.

Runtime policy is **ADPCM remains in XIP; Core 1 decodes incrementally into the existing mixer path**. Do not decompress complete sounds into SRAM. A playback channel keeps only small decoder state/cache. Looping and seeking use `sample_position / 256` to select a block, then decode within that block. The validated Core-1 display/input/audio ownership model remains unchanged; ADPCM decoding is an addition inside the audio source path, not a redesign of qservice or DMA.

The measured QAD1 footprint confirms that 256-sample independent blocks add only modest metadata overhead over the theoretical 4-bit payload, while providing bounded random access suitable for the mixer.

## Host tools and current Flash status

`Tools/RP2350Pack/mcu_pack_converter.py` performs portable host conversion including alias MDL and QAD1 sound conversion. `Tools/RP2350Pack/xip_image_builder.py` builds QXIP and physically deduplicates exact BSP miptex records into a global texture store.

Current measured shareware footprint after the implemented conversions:

- original aligned PAK: **17.43 MiB**;
- Python converted PAK: **15.60 MiB**;
- alias MDL: **2.70 -> 2.20 MiB**, saving about **0.50 MiB**;
- sound: **2.72 MiB original WAV -> 1.38 MiB QAD1 block IMA ADPCM**, saving about **1.34 MiB**;
- global exact BSP texture dedup: **1.15 MiB** real saving;
- generated QXIP image: **15,160,036 bytes = 14.46 MiB**;
- firmware reservation: **1.00 MiB**;
- asset budget: **15.00 MiB**;
- measured asset headroom: **about +0.54 MiB**.

The shareware assets therefore fit the 16 MiB Flash plan with the current 1 MiB firmware reservation. No lightmap compression is required for capacity at this stage. Further resource compression should be driven by a measured need rather than added pre-emptively.

## Migration phases and status

### Phase 0 - validated platform services: COMPLETE/FROZEN

Core-1 display/input/audio structure is the known-good platform layer.

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
8. QAD1 11025-Hz block IMA ADPCM conversion is implemented and measured: 190 WAV files / 251 s become about 1.38 MiB, saving about 1.34 MiB versus the original WAV set.
9. The generated QXIP asset image is now **14.46 MiB**, below the **15.00 MiB** asset budget with about **0.54 MiB measured headroom**.
10. Resource-capacity work is sufficient for Phase 1; lightmap compression is deferred because it is not currently required.
11. Core-1 architecture remains frozen; only the sound-source decoder will be added to its existing mixer path.

Immediate next work:

1. Integrate QXIP lookup into model/texture loading: level texture index -> global texture ID -> XIP miptex.
2. Implement the matching QAD1 block IMA decoder in the existing Core-1 mixer source path, including loop/seek behavior, without whole-sound SRAM decompression.
3. Remove RP2350 runtime flash-programming dependencies and load `start` directly through QXIP/XIP.
4. Continue the MG24 engine source integration and profile SRAM/XIP hot paths before adding further format changes.
5. Measure the final linked firmware size against the 1.00 MiB reservation; revisit asset compression only if the firmware or runtime metadata materially reduces the current ~0.54 MiB asset margin.

Phase 1 is not complete until the real engine links and `start` loads through the production XIP path.

### Phase 2 - restore MG24 world rendering: NOT STARTED

Bring up BSP/PVS -> edge -> surface -> span rendering into the single indexed framebuffer and hand completed frames to unchanged Core 1.

### Phase 3 - model/collision/game integration: NOT STARTED

Restore entities, alias models, sprites, particles, sky/turbulence, dynamic lighting, collision and gameplay using the MG24-derived paths.

### Phase 4 - proven host-side hot formats: IN PROGRESS

Alias MDL conversion, QXIP global texture dedup and QAD1 block IMA ADPCM are host-side transformations selected from measured RP2350 benefit. MG24 node/leaf packing is explicitly excluded. Capacity optimization is paused after reaching a measured 14.46 MiB QXIP image; additional transformations require profiling or a concrete Flash need.

### Phase 5 - spend extra SRAM for speed: NOT STARTED

Measure linker/runtime high-water use and XIP-sensitive paths, then enlarge caches/working sets within a documented budget.

### Phase 6 - RP2350 CPU optimization: NOT STARTED

Profile the MG24 baseline first, then tune Cortex-M33 loops, compiler options, SRAM placement and XIP access with before/after measurements.

## Completion criteria

Production must run the MG24-derived engine/renderer, retain the validated Core-1 service structure, use one indexed framebuffer plus two one-row RGB565 buffers, directly address runtime-ready immutable resources from 16 MiB XIP without level-change flash writes, use QAD1 sounds directly from XIP with incremental Core-1 decoding, keep native BSP nodes/leaves, and spend SRAM only on mutable/working/measured-hot data.
