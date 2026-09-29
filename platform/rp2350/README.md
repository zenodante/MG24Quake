# RP2350 Quake

The complete engine target is now `quake_rp2350`; see [build, resources, controls
and validation limits](game/README.md). It uses QRN1 resources at the 768 KiB
boundary. The historical notes below describe the separate bring-up target.

---

# RP2350 port — hardware baseline and resource/file integration

This target currently builds a **static textured world viewer and hardware
diagnostic**, not the complete Quake game. It runs at 200 MHz on the RP2350B Pimoroni Explorer and owns the MCU
and 16 MiB Flash directly. It does not use the pico8c bootloader.

The source is on branch `rp2350-port`. Original MG24 engine files are unchanged.
Board LCD/I2C drivers were copied from the local pico8c checkout
`05c235f60d5ad720fa88dc89e85a83462d83605a`. The current service core retains the user-validated p8-style blocking QwSTPad
transport, latched button presses, 128-sample audio DMA blocks and one-row LCD
ping-pong transfers. The original driver/source notices are retained.

## Implemented

- A lossless, independently decodable 4 KiB LZ4 asset container and bounded C
  reader with caller-owned caches. No full-file RAM decompression required.
- All 339 original shareware entries, including **demo1/2/3 and all 190 sounds**,
  preserved byte for byte. No music files exist in this PAK; nothing was deleted.
- PCM WAVs and BSP geometry/collision/visibility/entities remain directly XIP
  accessible. Only complete texture/lightmap blocks inside a BSP can compress;
  other eligible files use independently compressed blocks.
- No precomputed sky movies or expanded MG24 triangle skins are added.
- Core 1 exclusively owns LCD conversion/transfers, audio mixing/DMA/PWM and
  200 Hz TCA9555 input polling. Core 0 produces completed 320×200 indexed frames.
- One complete 8bpp frame with its palette snapshot and atomic ownership,
  plus two one-row RGB565 DMA buffers (320 pixels / 640 bytes each).
  Display centered at (0,20).
- A bounded sound-command queue and single-owner eight-channel PCM mixer,
  original WAV loop metadata, 8/16-bit 11025/22050 Hz sources, mono 22050 Hz PWM
  output (actual hardware divider approximates that rate), stop/fence handling.
  This is the output/mixing substrate: Quake spatialization and channel selection
  are **not wired into the engine yet**.
- Current button state and latched rising edges; failed reads preserve the
  last valid state. Raw button positions match pico8c QwSTPad.
- Fixed Flash layout shared by target, linker and host tools. Both firmware-only
  and combined UF2s place the RP2350-E10 guard outside resources and saves.
- Read-only Quake `Sys_File*` bridge with 16 independent file cursors and one
  shared 4 KiB decompression cache, used by the diagnostic's palette/background
  and sound loading. No heap or full-file decompression. All 339 files are tested
  against the original PAK through this interface.

## Measured results

| Item | Bytes |
|---|---:|
| Original PAK file | 18,689,235 |
| Original directory-referenced contents | 18,256,911 |
| New QPAK (all entries retained) | 14,600,880 |
| Reserved asset capacity | 15,466,496 |
| Remaining asset capacity | 865,616 |
| Current firmware binary | 71,292 |
| Current ELF BSS | 253,056 |

The linked diagnostic also reserves 4 KiB per core stack. These numbers do not
include the future game engine, its Z-buffer, entities or level arenas.
The complete game has **not** yet been measured or run on hardware. Asset fit
has been established, but any additional runtime asset metadata must fit within
the remaining resource capacity.

## Build using pico8c's SDK and compiler

From the MG24Quake root (adjust the reference checkout location if necessary):

```sh
cmake -S platform/rp2350 -B build-rp2350 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPICO_SDK_PATH=/Users/mingye/Documents/pico8c/third_party/pico-sdk \
  -DPICO_TOOLCHAIN_PATH=/Users/mingye/Documents/pico8c/third_party/toolchains/arm-gnu-toolchain-15.3.rel1
cmake --build build-rp2350 -j 6
```

CPU is 200 MHz (PLL 1.2 GHz, /6 /1, 1.10 V), PIO 32 MHz; Flash divider 4 keeps
the initial QMI clock conservative. SDK and toolchain remain in pico8c.
`quake_rp2350_bringup.uf2` is the firmware-only image. Normal builds neither
read nor regenerate QPAK. The linker limits firmware to `0xFF000` bytes and
post-build validation checks every UF2 destination, relocating the E10 guard
into its reserved sector. The physical board definition remains 16 MiB.

## Prepare the resources and combined image

Host Python uses an installed `liblz4`; alternatively pass
`--lz4-library /path/to/liblz4`. The image format records version/lengths and a
metadata CRC. Per-file CRC/SHA-256 and original PAK SHA-256 are in the manifest.
Host verification checks every byte against the source PAK; runtime open checks
structure/metadata, not the CRC of every entire file on each access.

```sh
python3 Tools/RP2350Pack/pack.py \
  /Users/mingye/Documents/MG24Quake-analysis/downloads/quake106/ID1/PAK0.PAK \
  assets-local/shareware.qpak
sh platform/rp2350/tests/run_host_tests.sh \
  /Users/mingye/Documents/MG24Quake-analysis/downloads/quake106/ID1/PAK0.PAK
python3 Tools/RP2350Pack/make_uf2.py \
  build-rp2350/quake_rp2350_bringup.uf2 assets-local/shareware.qpak \
  build-rp2350/quake_rp2350_bringup_with_assets.uf2
```

The combined UF2 is about 28 MiB on disk because UF2 doubles payload storage;
that is not Flash occupancy. Copyrighted input/output assets stay in ignored
`assets-local/` and build folders. No assets are embedded in tracked source.

## Hardware diagnostic behavior

With the existing resource image installed, the firmware opens start.bsp and
starts a 320x152 textured world view above a 48-row diagnostic/status area.
D-pad up/down move the camera forward/backward; left/right turn; +/- change
height. This is a free camera with no collision or gameplay. Y toggles the
original I/O diagnostic. A plays the shotgun sample, B stops it, X plays the
test tone in both views. These remain sound tests, not weapon gameplay.

Core 0 renders only into an acquired frame; core 1 handles the unchanged I/O.
USB reports rendering duration, faces/triangles, unsupported surfaces and service
counters. If the renderer/map cannot initialize, the original diagnostic remains
available. If assets are absent, it shows the palette/missing-QPAK fallback.

The user reports that the input, sound and display baseline has passed hardware
testing. The subsequent partition safeguards and file bridge have been compiled
and host-tested; this revision has **not yet been flashed or hardware-tested**.

## Flash layout 

| Physical offset | Purpose |
|---|---|
| 0x000000–0x0FEFFF | Firmware (up to 1,044,480 bytes) |
| 0x0FF000–0x0FFFFF | Dedicated RP2350-E10 UF2 guard sector |
| 0x100000–0xFBFFFF | Assets; XIP begins at 0x10100000 |
| 0xFC0000–0xFFFFFF | 256 KiB reserved for future saves/config |

`flash_layout.h` is the persistent layout ABI. QPAK v1 is unchanged; resource
positions depend only on the existing QPAK image, never on the firmware length.
The current image SHA-256 is
`2303343180f5e15a00f7a61c2b0a95e2a4e10941163ed139345e38e1d5312c2d`.

After the first combined-image installation, update **only**
`build-rp2350/quake_rp2350_bringup.uf2` via BOOTSEL. Do not regenerate or flash the
combined image for code-only changes. Existing resource bytes, directory offsets
and XIP addresses remain unchanged. A deliberate asset/format change is a
separate resource update; repacking different content does not promise identical
per-file offsets. No full-chip erase should be used for a firmware-only update.

Both UF2 build paths relocate Pico SDK's absolute-family E10 ignore block from
the last Flash page into the dedicated guard sector. No generated UF2 record
targets the save area. No Flash programming occurs during gameplay/test.
Persistent saves themselves are not implemented yet.

To validate/normalize an externally generated SDK firmware UF2 without resources:

```sh
python3 Tools/RP2350Pack/make_uf2.py --firmware-only input.uf2 firmware-only.uf2
```

Host tests simulate sector erase/program operations for firmware up to the
partition limit and check that all resource/save bytes remain unchanged.
Malformed UF2s and firmware that reaches the guard/resource partition are rejected.

## Original BSP29 integration

`qbsp.c` opens the original maps through QPAK without MG24's converted model
structures. Geometry, entities and PVS remain immutable XIP views; textures and
lightmaps use bounded reads with caller-owned decompression scratch. The loader
checks lump ranges/overlap, record sizes, geometry references, mip ranges and
finite numeric inputs. Disk records are decoded explicitly, not cast to native
runtime structs. It never writes Flash or allocates a whole map in RAM.

Available queries include model-space point-to-leaf, hull 0..3 point contents,
PVS decompression and original texture mip offsets. Cyclic trees terminate with
an error; malformed PVS runs fail. Lightmap sample extents still belong to the
future surface loader. Hull point queries are not swept collision traces.

The board diagnostic now opens `maps/start.bsp` and reports 5,556 faces and
1,568 leaves on screen. USB reports 55 models, 141 PVS bytes and the remaining
renderer integration status. Controls and audio behavior are unchanged.

Host verification covers all 21 BSP files (nine levels and twelve brush-model
files), each lump and texture mip, every leaf PVS row, and 254,000 hull queries
compared with the original PAK. Negative cases cover invalid versions, truncated
records, overlapping/out-of-range lumps, invalid indices, cycles and bad PVS
runs. Tests run with UndefinedBehaviorSanitizer. Optional
`QBSP_SANITIZERS=address,undefined` additionally enables AddressSanitizer on a
supported host; the installed macOS ASan runtime hangs during initialization,
so no ASan pass is claimed here.

The map access layer now feeds the reference world rasterizer described below.
Moving brush transforms, swept collision and live gameplay remain unimplemented.

## Static world rendering checkpoint

`qrender.c` implements a bounded reference path for original BSP assets: PVS
face marking, backface rejection, five-plane polygon clipping, fan
triangulation, 16-bit reciprocal depth, perspective-correct texture sampling,
and bilinear static-lightmap sampling through the original Quake colormap.
A mip level is increased only as needed to fit the 32 KiB texture buffer.
The renderer workspace is 163,984 bytes; large buffers and clipping scratch are
static rather than on the 4 KiB core stack.

This is not yet the optimized MG24 span renderer. It currently draws world model
zero only. Alias models, moving brush models, weapon/HUD, sky composition, water
warp, texture animation, animated lightstyles and dynamic lighting are not
implemented. Stored lightmap styles use a fixed unit intensity. Sky/water use
static texture sampling. The original gameplay/QuakeC code is not running yet.

Host tests render all nine level spawn views, 36 cardinal directions and 288
nearby camera samples; check frame canaries, deterministic redraw and invalid
camera rejection; and export screenshots for visual review. These are host
images, not hardware captures or a claim about device frame rate. Runtime
performance, stack high-water marks and sound continuity still need board
measurement. The build emits `.su` files for static stack-usage inspection.

The existing QPAK bytes and fixed layout are unchanged: code-only updates still
use the firmware-only UF2.

## File bridge integration boundary

`qfiles_mount()` receives the QPAK opened at the fixed resource address. Call the
`Sys_File*` interface on core 0 only; core 1 keeps immutable mapped PCM pointers.
Reads cross independently compressed blocks, clamp at EOF and maintain separate
cursors for simultaneous opens. `qfiles_map()` returns an immutable XIP pointer
only for fully uncompressed, contiguous ranges. Write opens fail explicitly.

The diagnostic now exercises this path. The full engine still needs its
`COM_FindFile`/loaders routed to QPAK-relative names, and its converted-format
pointer consumers replaced with original-format readers. In particular, this
bridge does not make MG24's `COM_LoadFileFromExtMem` valid for compressed files
and does not yet turn the target into a playable game.

## Next integration steps

See [PORTING_NOTES.md](PORTING_NOTES.md) for decisions based on the original
author's implementation article and the user-validated RP2350 baseline.

1. Adapt Quake's file/texture/model consumers to `qpak_read`/`qpak_map`.
   This image holds original BSP29/MDL/WAV data, **not MG24's converted format**.
   It cannot be renamed to PAK0.PAK and used unchanged by the MG24 engine.
2. Remove internal-Flash cache assumptions; use raw XIP structures where
   possible and bounded RAM arenas for necessary writable/derived structures.
3. Restore native runtime pointers with measured allocator/entity layout costs;
   keep compact on-disk indices where useful. Do not globally inflate every ID.
4. Restore runtime sky composition and original model skin sampling; retain the
   generated QuakeC gameplay implementation.
5. Connect VID, key events and Quake sound/spatial state to the service core.
   Wire the existing demos to timedemo, then validate start/e1m1–e1m8.

The storage format and platform layer now give those steps concrete interfaces
and a known resource budget. Do not describe the diagnostic as a playable port.
