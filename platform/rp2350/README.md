# RP2350 port — first implementation milestone

This target currently builds a **hardware and resource diagnostic**, not the
Quake game. It runs at 200 MHz on the RP2350B Pimoroni Explorer and owns the MCU
and 16 MiB Flash directly. It does not use the pico8c bootloader.

The source is on branch `rp2350-port`. Original MG24 engine files are unchanged.
Board LCD/I2C drivers were copied from the local pico8c checkout
`05c235f60d5ad720fa88dc89e85a83462d83605a`. The I2C transactions now have bounded
timeouts so disconnected controls cannot stall sound service. The original
driver/source notices are retained.

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
- Two complete 8bpp frames with per-frame palette snapshots and atomic
  ownership, plus two 8-row RGB565 DMA buffers. Display centered at (0,20).
- A bounded sound-command queue and single-owner eight-channel PCM mixer,
  original WAV loop metadata, 8/16-bit 11025/22050 Hz sources, mono 22050 Hz PWM
  output (actual hardware divider approximates that rate), stop/fence handling.
  This is the output/mixing substrate: Quake spatialization and channel selection
  are **not wired into the engine yet**.
- Input edge snapshots, held-state recovery, disconnect release, retry and
  overflow/error counters. Raw button positions match pico8c QwSTPad.
- Capacity-checked combined UF2 with an RP2350-E10 guard outside the save area.

## Measured results

| Item | Bytes |
|---|---:|
| Original PAK file | 18,689,235 |
| Original directory-referenced contents | 18,256,911 |
| New QPAK (all entries retained) | 14,600,880 |
| Reserved asset capacity | 15,466,496 |
| Remaining asset capacity | 865,616 |
| Current diagnostic firmware binary | 43,136 |
| Current diagnostic ELF BSS | 154,928 |

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
`quake_rp2350_bringup.uf2` is the firmware-only image.

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

After flashing the combined UF2 through RP2350 BOOTSEL, the display should show
the Quake console background, an explicitly labelled I/O test screen, a moving
bar and ten button indicators. A plays the original shotgun effect; B stops it.
Core 0 sends frames while core 1 handles the hardware. USB CDC reports frame,
audio refill, underrun, I2C error and input overflow counts once per second.
If assets are absent, it shows a palette pattern and a missing-QPAK message.

This artifact has been compiled and checked on the host but **has not been
flashed or observed on the physical board**.

## Flash layout 

| Physical offset | Purpose |
|---|---|
| 0x000000–0x0FEFFF | Firmware (up to 1,044,480 bytes) |
| 0x0FF000–0x0FFFFF | Dedicated RP2350-E10 UF2 guard sector |
| 0x100000–0xFBFFFF | Assets; XIP begins at 0x10100000 |
| 0xFC0000–0xFFFFFF | 256 KiB reserved for future saves/config |

The combined-image tool relocates Pico SDK's absolute-family E10 ignore block
from the last Flash page into the dedicated guard sector. No generated UF2
record targets the save area. No Flash programming occurs during gameplay/test.
Persistent saves themselves are not implemented yet.

## Next integration steps

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
