# Complete RP2350 Quake firmware

## Latest updates

- **2026-09-29:** removed two legacy debug stops in normal changelevel, added episode-portal and continuous transition regressions, and added native resource pointer alignment checks. Firmware payload: **613,624 bytes**. See [transition validation](RP2350_CHANGELEVEL_FIX.md).
- **2026-09-28:** fixed the unaligned clipping workspace that caused a HardFault after teleporting in start. That build was 613,696 bytes; resources were unchanged. Three difficulty portals each passed 60 ARM frames and 44 clipped-submodel calls. The user subsequently confirmed the difficulty portal works on hardware. See [fault investigation](RP2350_TELEPORT_DIAGNOSTICS.md).

The `quake_rp2350` target includes the complete MG24 single-player engine: game logic, compiled QuakeC, optimized rendering, collision, entities, menus and sound. It is not the earlier world viewer, bring-up application or size-only ELF.

Hardware startup and the difficulty portal are confirmed. The latest episode-transition fix still needs hardware retesting. ARM emulation checks engine execution; LCD/DMA, audio timing, electrical input wiring and actual frame rate require device testing.

## Flashing

1. `RP2350-Quake-Resources-QRN1.uf2`: resources starting at `0x100C0000`.
2. `RP2350-Quake-Firmware-768KiB.uf2`: firmware starting at `0x10000000`, including the E10 guard at `0x100BF000`.

Install both on first use or when migrating from the old layout. Enter BOOTSEL for each copy if the device restarts between them. Do not mix these with old QXIP resource UF2s. Later firmware-only updates are sufficient if resources and ABI remain unchanged. Native resources contain fixed ARM pointers and cannot be moved to another Flash base.

The board is Pimoroni Explorer / RP2350B / 16 MiB Flash, not a generic Pico configuration. ST7789: CS27, DC28, WR30, RD31, data32–39, backlight26. QwSTPad: I2C0 GPIO20/21, address0x21. Peripheral startup currently reports an error if the gamepad is missing.

## Baseline measurements

The following table records the original complete-engine/keymap build, before the later diagnostic and transition fixes. Do not treat its SRAM figures as new measurements of those later builds. Current firmware Flash size is listed above and in release verification reports.

| Item | Bytes | Notes |
|---|---:|---|
| Firmware Flash | 611,968 | 597.62 KiB |
| Firmware limit | 782,336 | 768 KiB reservation minus 4 KiB guard |
| Firmware headroom | 170,368 | Does not use the resource partition |
| Native resources | 15,879,760 | 339 files, 21 BSP, 61 alias, 3 sprite |
| Resource headroom | 111,024 | 108.42 KiB |
| Allocated SRAM sections | 439,248 | Includes both stacks and 2 KiB minimum libc heap |
| Core 0 / core 1 stacks | 32,768 / 4,096 | Actual SRAM, no enlarged RAM linker region |
| libc heap growth capacity | 91,180 | Includes the minimum 2 KiB; do not add it twice |
| Game zone capacity / observed peak | 131,072 / 43,112 | Zone is already included in BSS |
| Compatibility metadata capacity / peak | 32,768 / 13,944 | Pool is already included in BSS |

UF2 containers are roughly twice the Flash payload size. Budgets use programmed data, not container size. Runtime peaks come from short nine-map regressions, not every possible gameplay situation.

## Immutable resources

The host uses actual ARM headers/compiler to generate planes, nodes, leaves, texinfo, surfaces, texture animation pointers, clipnodes, hull0, inline models and alias/sprite descriptors at fixed resource addresses. Texture pixels are deduplicated. The WAD directory is normalized offline and font pixels are read directly from XIP. Generated `quake-native.h` provides resource entry addresses.

Firmware verifies ABI and CRC at startup and binds XIP data on level changes. There are no runtime pointer relocations or per-level Flash writes. Host-only 15 MiB/64 MiB simulated Flash arrays were removed from the target.

The remaining metadata pool is not a geometry cache or Flash writer. It stores mutable model load flags/registries, precache lists, small UI descriptors and runtime entity-field copies retained from MG24. Further work can separate fixed resource IDs from mutable flags. Entity position, health, AI, dynamic lights, spatial lists, rendering buffers and mixer state must remain writable.

## Cores and controls

Core 0 runs game logic and rendering. Core 1 owns a single 320×200×8-bit framebuffer during display, two 320-pixel RGB565 rows, LCD, input and ADPCM/PWM. Core 0 waits for framebuffer ownership before drawing.

D-pad left/right turns; up/down moves. Y/A strafe left/right; B fires; X selects the next weapon; Minus/Plus look down/up. All ten buttons serve those actions; jump/menu are not assigned. USB serial accepts commands such as `map e1m3` and `skill 2`.

Eight mixer channels provide four dynamic effects, two loudest static loops and two ambient loops, with position-dependent attenuation. PWM is mono. Persistent saves/settings are disabled because there is no save partition. No multiplayer or music was added.

## Validation history

The original 540-frame nine-map regression tested the complete-engine baseline. The later keymap ELF had a separate 130-frame matrix test checking press/release for nine held actions and one weapon impulse. Reports identify the corresponding ELF/UF2 hashes; these are distinct builds.

- Baseline ARM execution: start and e1m1–e1m8, 540 frame submissions, active local servers and signon=4; movement, firing, damage and 264 dynamic sound requests occurred.
- Separate earlier input test: 60 submissions with the old A-button/fire binding and forward movement; shells fell from 25 to 20, confirming bindings were not lost in the 64-byte command queue.
- Emulated Flash is read-only. Peripherals, time and RP2350 DCP double helpers are intercepted, so this is not hardware timing validation.
- Initial CRC/pointer/model corruption checks: four passed. Alignment corruption coverage was added later. UF2 partition/guard checks: four passed.
- PCM/QAD1 decoding, looping and sample timing tests passed.
- The Mac full engine rebuilt and passed a 180-frame start regression.
- Both UF2s reconstruct the original bytes, with family ID, address, guard and boundary checks passing.
- Latest transition tests: the previous firmware reproduces HOST CHANGE LEVEL at the episode portal; fixed firmware enters e1m1 and passes 450 frames of changelevel across nine maps.

Reports record SHA-256 for ELF, resources and UF2s. ARM screenshots are emulator output, not device photographs.

## Rebuild

From the repository root:

```sh
python3 Tools/RP2350Pack/build_game_firmware.py build/pak0.pak \
  -o build-host/rp2350-release
```

SDK, ARM GCC and picotool default to sibling `pico8c`. Generate resources alone with `run_pipeline.py --resource-profile rp2350-game`. See [the implementation guide](../platform/rp2350/game/README.md) and [release regression options](RP2350_CHANGELEVEL_FIX.md).
