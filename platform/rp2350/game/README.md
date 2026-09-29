# RP2350 full MG24 Quake engine

`quake_rp2350` links the complete MG24 single-player engine, compiled QuakeC,
optimized software renderer, collision, client/server loop and menu system.
The ARM assembly rendering branches use the existing equivalent C paths
(`QMAC_RENDER_C=1`). Native pointers are 32-bit; compressed SRAM pointers are off.
The older `quake_rp2350_bringup` and `quake_rp2350_engine_phase1` targets remain
separate diagnostics. The size-audit ELF is not this firmware.

## Build

From the repository root, with the SDK/toolchain already in sibling `pico8c`:

```sh
python3 Tools/RP2350Pack/build_game_firmware.py build/pak0.pak \
  -o build-host/rp2350-release
```

The command converts the PAK, deduplicates texture pixels, creates QAD1 audio,
compiles native ARM resources and builds/verifies two independent UF2 files:

- `firmware/game/quake_rp2350.uf2`
- `resources/quake-resources.uf2`

Resource-only regeneration:

```sh
python3 Tools/RP2350Pack/run_pipeline.py build/pak0.pak \
  -o build-host/rp2350-game-assets --resource-profile rp2350-game
```

The pipeline's QXIP3 is an intermediate representation. **Flash the final QRN1
resource UF2 with this firmware, not an older QXIP or Mac QRES UF2.**
`quake-native.h` supplies fixed entry addresses. The native resource link uses
the actual engine headers and ARM compiler to resolve pointers and bitfields.
No runtime pointer relocations, brush geometry allocation, or per-level Flash
programming occur. The compiler normalizes the WAD directory too.

## Flash and SRAM

- Program reservation: 768 KiB, with its last 4 KiB reserved for the UF2 E10 guard.
- Program link limit: 782,336 bytes.
- Resources start: `0x100C0000`; capacity: 15,990,784 bytes.
- Native image: 15,879,760 bytes for the provided shareware PAK.
- No PSRAM and no enlarged/synthetic SRAM linker region.
- Core 0 stack: 32 KiB in main SRAM; core 1 stack: 4 KiB in scratch X.
- Zone allocator: 128 KiB, counted inside BSS, for mutable entities/state.
- Remaining compatibility metadata arena: 32 KiB, counted inside BSS, never a
  Flash writer. It holds per-session model/precache registries, UI descriptors
  and MG24's runtime-selected entity ROM field copies. It resets at map load.
- BSP nodes/leafs/planes/texinfo/surfaces, both collision hull forms, inline brush
  descriptors, alias/sprite metadata, texture animation pointers, WAD directory
  and font pixels reside directly in XIP.

`Mod_ClearAll` changes model load flags, so the live model registry remains
mutable. It copies small descriptors whose payload pointers already target
XIP; it does not copy their geometry. Dynamic lights, entity state, visibility
workspaces, rasterizer buffers, framebuffer and mixer state remain in SRAM.

## Hardware and controls

This build uses the existing **RP2350B Pimoroni Explorer / 16 MiB Flash** profile
and the pico8c-derived peripheral wiring, not a generic Pico board:

- ST7789 320×240 parallel LCD: CS 27, DC 28, WR 30, RD 31, D0..D7 32..39,
  backlight 26. The 320×200 game image is centered vertically.
- QwSTPad: I2C0 GPIO20/21, address `0x21`; required by current service startup.
- Audio uses the existing board PWM/DMA and amplifier enable GPIO13.

Core 0 executes the game and renderer. Core 1 owns the single 64,000-byte
indexed framebuffer during LCD transfer, converts through exactly two 320-pixel
RGB565 rows, polls keys and decodes/mixes ADPCM. Core 0 waits for ownership
before drawing again. A missing service startup is reported on USB serial.

| Control | Action |
|---|---|
| D-pad up/down | Forward/back |
| D-pad left/right | Turn |
| Y | Strafe left |
| A | Strafe right |
| B | Attack |
| X | Next weapon |
| Minus (−) | Look down |
| Plus (+) | Look up |

All ten pad buttons now serve the requested gameplay mapping. Jump and menu
selection are not assigned to the pad. USB serial accepts console commands such as `map e1m3`, `skill 2`, `impulse 10`.
The 8-voice mixer assigns four dynamic effects, two loudest static loops and two
leaf ambient loops. Attenuation/volume update as the listener moves. PWM output
is mono. Persistent saves/settings are disabled: this partition layout has no
save area, and the old MG24 Flash erase/program path is not safe here. Network
multiplayer and music are not added to the MG24 single-player baseline.

## Validation and limits

`verify_arm_native.py` checks CRC, directory and native graph references;
`tests/test_arm_native.py` also rejects corrupted CRC/pointers/model entries.
`verify_split_uf2.py` reconstructs both binaries, checks family/address/guard
records and proves the write regions do not overlap.

An optional CPU test runs the **linked ARM ELF** in Unicorn with read-only Flash:

```sh
python3 -m venv build-host/arm-test-venv
build-host/arm-test-venv/bin/pip install unicorn pyelftools
build-host/arm-test-venv/bin/python Tools/RP2350Pack/test_arm_firmware.py \
  build-host/full-firmware/game/quake_rp2350.elf \
  build-host/rp2350-game-assets/quake-native.qrn \
  --frames 540 --cycle 60 --json build-host/arm-test.json
```

It exercises initialization, nine map loads, signon, rendering, movement and
attack. Peripherals/time and RP2350 DCP double operations are intercepted;
normal ARM/VFP game execution is emulated. This is **not** a ROM boot, LCD/DMA,
input timing, audio underrun, FPS or thermal test. The generated firmware still
requires physical board validation; no physical flashing was performed here.

To verify every pad bit against the engine input state (including release and
weapon impulse), run the ARM test above with `--frames 130 --input-matrix`
instead of `--frames 540 --cycle 60`.
