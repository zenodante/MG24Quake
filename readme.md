# Quake for RP2350 — Based on MG24Quake

A single-player Quake port for **RP2350B / Pimoroni Explorer with 16 MiB Flash and no external RAM**, with a macOS SDL test application and an offline PAK resource compiler.

The port retains MG24Quake's optimized software renderer. Immutable resources are expanded, deduplicated and assigned fixed XIP Flash addresses on the host. Runtime level loading does not erase or program Flash, and MG24's compressed SRAM pointers are disabled. Hardware validation is ongoing; a complete playthrough has not been verified.

![Quake start map rendered by the actual ARM firmware in emulation](docs/images/quake-start-arm.png)

*320×200 output from the linked RP2350 ARM firmware running in instruction emulation. Peripheral services are intercepted by the test harness. This is not a hardware photograph or a performance measurement.*

## Origins and credits

This project continues **[next-hack/MG24Quake](https://github.com/next-hack/MG24Quake)** by **Nicola Wrachien (next-hack)**. That port targets Silicon Labs EFR32MG24/MGM240 and extensively optimizes rendering, resources and game-state storage for approximately 276 KiB RAM. Its foundations are **id Software's Quake / WinQuake** and SDLQuake.

- Upstream code and commit history are preserved. The [original README](docs/UPSTREAM_README.md) is archived; its hardware, formats and feature claims do not describe this RP2350 version.
- This port adds RP2350 dual-core peripheral services, a Mac SDL application, an offline native resource compiler, and separate firmware/resource UF2 generation and verification.
- LCD scanline buffering, input and audio services draw on the local `pico8c` project.
- Existing copyright and license notices are retained. See [LICENSE](LICENSE) and the [Quake engine COPYING](QuakeMG24/Quake/COPYING). Game data licensing is separate from the source license; provide lawfully obtained game resources.

## Architecture

- **Core 0:** game logic, local server/client, collision, QuakeC compiled to C, and the optimized MG24 renderer. Equivalent C paths replace ARM assembly rendering branches (`QMAC_RENDER_C=1`).
- **Core 1:** LCD output, input polling, ADPCM decoding and mixing. A single 64,000-byte indexed framebuffer feeds two 320-pixel RGB565 scanline buffers.
- **Offline resources:** PAK conversion, texture deduplication and audio encoding. Engine headers and the ARM compiler determine native structures, pointers and bitfields stored directly in XIP.
- **SRAM:** mutable entities, game state, rendering workspaces, audio state and necessary registries. Native pointers are 32-bit; no 16-bit SRAM pointer compression is used.
- **Mac:** SDL's main thread handles display, input and audio; a worker runs the engine. Mac RSS and 64-bit structures are not RP2350 SRAM measurements.

Resource pipeline: `PAK → conversion/deduplication → QXIP3 intermediate → ARM-native QRN1 → resource UF2`. The firmware uses QRN1; do not substitute older QXIP or Mac QRES images.

## Build and flash

The board configuration targets **Pimoroni Explorer RP2350B**, not a generic Pico: ST7789 parallel LCD, QwSTPad on I2C0 GPIO20/21 at `0x21`, and PWM audio. See the [full firmware guide](platform/rp2350/game/README.md) for wiring, stacks and memory layout.

Requirements: Python 3, CMake, Ninja, Pico SDK, ARM GNU toolchain and picotool. Scripts default to SDK/toolchain files in sibling `../pico8c`; override with `--pico8c-root` and `--picotool-dir`.

From the repository root, using an original Quake PAK:

```sh
python3 Tools/RP2350Pack/build_game_firmware.py build/pak0.pak \
  -o build-host/rp2350-release
```

Two independently flashable files are generated:

| File | Purpose |
|---|---|
| `build-host/rp2350-release/firmware/game/quake_rp2350.uf2` | Game firmware |
| `build-host/rp2350-release/resources/quake-resources.uf2` | Native QRN1 resources |

Copy each UF2 in BOOTSEL mode. If the board restarts after the first copy, enter BOOTSEL again for the second. First installation requires both files. Firmware-only updates are sufficient when the resource format and contents have not changed.

Prebuilt files are available in [GitHub Releases](https://github.com/zenodante/MG24Quake/releases): `RP2350-Quake-Firmware-768KiB.uf2` and `RP2350-Quake-Resources-QRN1.uf2`.

| Flash allocation | Size / address |
|---|---|
| Firmware reservation | 768 KiB, including a final 4 KiB UF2 E10 guard |
| Usable firmware capacity | 782,336 bytes |
| Resource start | `0x100C0000` |
| Resource capacity | 15,990,784 bytes (15.25 MiB) |

The 2026-09-29 build contains **613,624 bytes** of firmware and **15,879,760 bytes** of resources from the tested shareware PAK. These are Flash payload sizes; UF2 containers are larger. Consult generated reports for other inputs or builds.

## Controls

| Button | Action |
|---|---|
| Left / Right | Turn |
| Up / Down | Move forward / backward |
| Y / A | Strafe left / right |
| B | Fire |
| X | Next weapon |
| − / + | Look down / up |

Jump and menu operations are not currently assigned to the gamepad. USB serial accepts console commands.

## Mac application

Current scripts use Homebrew SDL2 at `/opt/homebrew/bin/sdl2-config`:

```sh
python3 Tools/RP2350Pack/run_pipeline.py build/pak0.pak \
  -o build-host/mac-game-resources --resource-profile mac-game
build-host/mac-game-resources/host-tools/quake_game \
  --assets build-host/mac-game-resources/quake-resources.qres --map e1m1
```

W/S move, A/D strafe, Left/Right turn, Space jumps, Ctrl fires and Esc opens the menu. See the [Mac SDL guide](platform/macos/README.md) for options and tests.

## Validation and limitations

Hardware startup and the difficulty-selection portal have been confirmed. Fixes cover an unaligned clipping buffer that caused a HardFault and two legacy debug stops in normal level transitions. The latest transition fix passes the episode portal into `e1m1` and a 450-frame, nine-map ARM changelevel regression; **hardware retesting of that fix is pending**.

Resource checks cover CRC, image bounds, native pointer alignment and selected model graph references. Firmware tests cover controls, teleportation and level transitions. Instruction emulation replaces peripheral services and cannot establish correct dual-core, LCD, DMA or hardware timing behavior.

Enable gameplay regression gates for a release build using Python with `unicorn` and `pyelftools` installed:

```sh
python3 Tools/RP2350Pack/build_game_firmware.py build/pak0.pak \
  -o build-host/rp2350-release \
  --engine-test-python build-host/arm-test-venv/bin/python
```

Any failing regression prevents a new success report. Without this option, the report explicitly records `engine_tested=false`.

There is no persistent save/settings partition, multiplayer or CD music. Validation uses shareware resources; upstream MG24 retail-game support does not establish retail compatibility for this RP2350 configuration.

## Documentation

- [Documentation index](docs/README.md)
- [Firmware size, memory and validation records](docs/RP2350_FULL_FIRMWARE_RESULTS.md)
- [Level-transition fixes and release validation](docs/RP2350_CHANGELEVEL_FIX.md)
- [Teleport HardFault investigation](docs/RP2350_TELEPORT_DIAGNOSTICS.md)
- [Intermediate resource format](docs/QXIP_RESOURCE_FORMAT.md)

The complete game target is `quake_rp2350`. Retained bring-up, phase1 and size-audit projects are diagnostics or historical measurements, not substitutes for the game firmware.
