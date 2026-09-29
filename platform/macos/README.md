# Mac: MG24 renderer with offline-expanded read-only resources

There are two entry points. `quake_game` runs the complete MG24 Host_Frame, local server/client, Quake logic compiled to C, players/monsters/weapons/particles, HUD and sound scheduling. `quake_mac` remains a world-rendering and resource-format regression application. Both use MG24's original C edge-scan, surface/span and alias/sprite paths; neither executes ARM assembly on the Mac.

## Full game: build and run

From the repository root:

```sh
python3 Tools/RP2350Pack/run_pipeline.py build/pak0.pak \
  -o build-host/mac-game-resources --resource-profile mac-game
build-host/mac-game-resources/host-tools/quake_game \
  --assets build-host/mac-game-resources/quake-resources.qres --map e1m1
```

W/S move, A/D strafe, Left/Right turn, Space jumps, Ctrl/left mouse fires, number keys select weapons and Esc opens the original menu. Close the window to exit. Mouse-look is not implemented. Without `--frames`, execution continues indefinitely, scheduled at 60 Hz by default. SDL's main thread handles display, input and QAD1 decoding/mixing; a worker runs logic/rendering.

The full game requires the `mac-game` profile. It produces IDPX alias models whose original indexed skins stay in mapped resources and are sampled through MG24's direct-memory path, rather than SPI-specific per-triangle skin streams. The older `mac` profile produces IDPM for resource/world-renderer regression and must not be substituted for full-game resources. Packing provides an accessible trailing NUL for `.cfg/.rc/.txt/.ent` and QLV entities while directory/section lengths retain the original text length.

Repeatable full-game checks:

```sh
python3 platform/macos/test_game.py \
  --player build-host/mac-game-resources/host-tools/quake_game \
  --assets build-host/mac-game-resources/quake-resources.qres \
  --output build-host/mac-game-resources/validation
```

This executes start and e1m1–e1m8 for 360 Host_Frame calls each, checks signon 4, ammunition consumption, ADPCM and buffer sizes, and saves JSON, logs and screenshots. It does not establish complete playthroughs, save/load, all menus or every enemy behavior. `build_game.py --sanitize` enables AddressSanitizer, but this machine's ASan runtime deadlocked during dyld/malloc initialization before main; that is not a passed memory-safety test.

For the tested original pak0, QXIP was **15,826,560 B** (**97,920 B** over 15 MiB), 64-bit QNAT **5,465,448 B**, and full-game Mac QRES **21,308,776 B**. QRES contains QLV, host-native structures and relocations; it is not a hardware image. `mac-game` permits oversized host packages but reports exact sizes; `--require-flash-fit` rejects oversized complete packages. Those measurements used a 1 MiB firmware / 15 MiB resource reference. RP2350 now reserves 768 KiB for firmware and 15.25 MiB for compact ARM QRN1; see [current firmware](../rp2350/game/README.md).

Memory reports separate zone, globals, Z-buffer, services and stacks. Framebuffer/scanlines are service subitems and must not be counted twice. Reports include legacy simulated Flash reservations and per-level alias/sprite/UI/registry metadata. **Not all legacy Mac construction paths have moved offline.** There are no hardware Flash writes, but some per-level metadata is still built. Mac RSS, 64-bit layouts and system libraries are not RP2350 SRAM measurements.

## Offline immutable structures

```text
Original PAK → Python conversion/deduplication → QXIP3 (QLV1 + TEX1 + LMAP)
                                             → qnative_pack (engine ABI) → Mac QNAT1
                                             → qlevel_assets.h / qnative_assets.h
```

The standalone qnative_pack uses actual engine C layouts to generate planes, nodes, leaves, surfaces, texinfo, texture animation pointers and model/brush entries. It covers every inline brush model in each BSP and generates hull 0 and compressed clipnodes for full-game collision. The world viewer can still use QLV1 collision records.

It emits explicit relocation records for bound pointer fields, rather than scanning memory to guess pointers. Texture pixels, vertices, edges, surfedges, marksurfaces, PVS, light samples and entities reference deduplicated QXIP instead of being copied into the native-structure image.

At startup the application checks the paired QXIP CRC, QNAT CRC, ABI and relocation bounds, maps QNAT with MAP_PRIVATE, adjusts pointers for both actual mapping bases and applies mprotect(PROT_READ). **Large brush arrays bind directly on level changes; the full game still copies small model registry entries.** The world viewer does no runtime expansion. The game's QLV path bypasses Mod_LoadBrushModel; remaining alias/sprite legacy paths are subject to the limitations above.

Mac ASLR prevents fixed absolute addresses ahead of time, so startup relocation simulates fixed Flash pointers. QNAT consumes real host RAM, reported separately as simulated Flash while retaining actual RSS. The later RP2350 implementation generates 32-bit fixed-address QRN1; Mac QNAT itself **cannot be flashed to RP2350**.

qnative_assets.h provides per-BSP model byte offsets, ABI and image SHA256; the native base resolves entries. qlevel_assets.h provides local texture → global TEX1 maps. Compiling entry headers or storing a directory in the read-only image serves the same purpose: relationships are determined offline without equivalent runtime SRAM pointer tables.

QXIP retains general QLV1 while Mac QNAT is a separate ABI-validation companion, so some immutable metadata exists in two disk representations. Target images must replace corresponding source records and recalculate capacity using the target ABI. Adding these Mac files does not demonstrate 16 MiB Flash fit.

## World viewer: build and run

Requires Apple clang, Python 3 and SDL2 (currently Homebrew SDL2). From the repository root, the PAK pipeline builds offline tools, expands structures and generates one complete package by default:

```sh
python3 Tools/RP2350Pack/run_pipeline.py build/pak0.pak -o build-host/complete-resources
build-host/complete-resources/host-tools/quake_mac \
  --assets build-host/complete-resources/quake-resources.qres
```

A separate `--native` argument is unnecessary. `--native-packer /path/to/qnative_pack` reuses a compiled tool; `--resource-profile qxip-only` explicitly selects the older QXIP-only pipeline, including BSP29 compatibility.

Uncompressed QRES1 contains QXIP3 and QNAT1 with 16 KiB section alignment for read-only/private mappings. qresource_package.h provides both entries; qnative_assets.h and qlevel_assets.h are also generated. resource-package.json and summary.json include relocations, directories and padding in complete-package sizes. QXIP headroom alone is not complete-package headroom.

Historical world-viewer package measurement: **20,184,616 B (19.2495 MiB)**.

| Component | Bytes |
|---|---:|
| Deduplicated QXIP3 | 15,424,320 |
| Offline QNAT1 including relocations | 4,734,504 |
| QRES header | 64 |
| Alignment padding | 25,728 |
| Total | **20,184,616** |

This exceeded the then-reference 15,728,640 B (15 MiB) resource partition by **4,455,976 B**. It is a validated 64-bit Mac test package, not a target-flashable image. Target resources require 32-bit ABI replacement and deduplication of representations.

`--require-flash-fit` returns nonzero, writes size reports and refuses to publish a new oversized package, preserving any existing package. Default Mac mode permits packages larger than the reference hardware capacity while explicitly reporting the excess.

```sh
python3 Tools/RP2350Pack/resource_package.py build-host/complete-resources/quake-resources.qres
python3 Tools/RP2350Pack/tests/test_resource_package.py build-host/complete-resources \
  --player build-host/complete-resources/host-tools/quake_mac
```

Tests passed for determinism, exact size accounting, corrupt-package rejection, preserving the old package on overflow, and a single-package nine-map/180-frame run. Frame hashes matched the separate-file version. Dynamic checks used a separately built UBSan application.

For an existing QXIP, run only the native stage:

```sh
python3 Tools/RP2350Pack/native_image.py build-host/preexpanded/quake-assets.qxip \
  --packer build-host/macos/qnative_pack \
  -o build-host/preexpanded/quake-assets-mac.qnat \
  --header build-host/preexpanded/qnative_assets.h \
  --json build-host/preexpanded/quake-assets-native.json
```

CMake entry points remain: `cmake -S platform/macos -B build-host/macos`, then `cmake --build build-host/macos -j6`. CMake path resolution stalled on the development machine; the direct clang scripts above were used for the actual complete builds and validation.

QMAC_RENDER_C=1 in mg24_config.h selects upstream's equivalent host C paths. Upstream labels these WIN32, which does not imply Windows API calls. ARM assembly is disabled on Mac. QMAC_MG24_RENDERER is the CMake renderer switch, ON by default; OFF selects the early triangle reference renderer. SRAM short-pointer macros are disabled; BSP/workspace element numbers remain ordinary indices.

World-viewer controls: W/S move, A/D strafe, Left/Right turn, Q/E move vertically, Tab changes map, Space plays test ADPCM and Esc exits. The camera has BSP collision but no player physics/gravity. Sky samples original read-only two-layer pixels directly rather than allocating a 2 MiB precomposed sequence. Water retains MG24's turbulence span path.

## Threads and memory

The design follows pico8c SDL RGB565 scanline upload and queued audio. Under macOS video-thread requirements:

- The worker simulates core 0: collision camera and MG24 rendering.
- SDL's main thread simulates core 1: keyboard, two RGB565 scanlines, QAD1 decoding/mixing and audio queueing.
- There is one 320×200 8-bit framebuffer; mutex/condition synchronization transfers ownership.
- The 320×152 16-bit Z-buffer retains phased reuse of unused areas for visibility/edge data.
- SDL textures and driver buffers emulate display hardware; they are not additional engine framebuffers.

Mac pointers are 64-bit, making edges larger than on the MCU. Span capacity is 8191, the 13-bit index limit. Reports measure actual stack high-water usage on a 256 KiB guarded worker stack rather than treating reservation as consumption. Capacity overflow fails explicitly rather than silently truncating rendering. Fixes include valid sentinel elements, 64-bit alignment, unsigned packing and saturating conversions equivalent to ARM VCVT; MG24's core rendering algorithm remains intact.

Historical local shareware world-viewer measurements, non-sanitized, nine maps/180 frames:

| Item | Bytes |
|---|---:|
| Indexed framebuffer | 64,000 |
| Two RGB565 scanlines | 1,280 |
| Z-buffer including phased reuse | 97,280 |
| Writable static sections including padding | 229,904 |
| Observed worker stack high-water mark | 61,872 |
| Per-level immutable metadata heap allocations | **0** |
| Read-only QXIP mapping | 15,424,320 |
| Simulated QNAT Flash, 21 BSPs | 4,734,504 |
| Startup pointer relocations | 146,778 |

Static storage plus observed worker stack is about 285 KiB, **not a full-game or RP2350 SRAM budget**. It excludes additional game state, SDL/OS, main-thread stack and allocator overhead; simulated Flash is separate. memory_report.py uses the Mach-O linker map to include globals scattered across engine files, rather than undercounting with a few sizeof expressions. Sanitizers add writable metadata; use release measurements for budgeting.

```sh
python3 platform/macos/build.py --release -o build-host/macos-release
build-host/macos-release/quake_mac \
  --assets build-host/preexpanded/quake-assets.qxip \
  --native build-host/preexpanded/quake-assets-mac.qnat \
  --headless --frames 180 --cycle 20 --scripted \
  --report build-host/macos-release/runtime-memory.json
python3 platform/macos/memory_report.py \
  build-host/macos-release/quake_mac.map \
  build-host/macos-release/runtime-memory.json \
  -o build-host/macos-release/memory.json
```

## Validation

```sh
python3 platform/macos/test_native.py build-host/macos \
  build-host/preexpanded/quake-assets.qxip build-host/preexpanded/quake-assets-mac.qnat
python3 Tools/RP2350Pack/tests/test_runtime.py build-host/preexpanded/pak0conv-python.pak
```

Verified: byte-identical offline compilation from identical inputs; identical 180-frame hashes at different mapping bases; 6,480 frames over nine playable maps with 360-degree turns and no UBSan errors; rejection of six corrupt/mismatched images; unchanged source images on disk. Twelve of the 21 BSPs are separate brush resources without player starts: they are expanded but are not standalone playable maps. Fixed frame-hash regression uses the local shareware assets.

Seven resource-conversion tests also independently checked 1,962 texinfo and 42,398 surfaces against C loader mathematics. ASan's system-runtime initialization lockup was not counted as success; UBSan provided dynamic checking.

Legacy QPAK/BSP/mixer/reference-renderer tests passed. Collision passed 72,000 swept traces against a recursive Quake reference. The historical UF2 artifact combination test required local build-rp2350/quake_rp2350_bringup.uf2, which was absent and therefore not counted as passed; RP2350 firmware was not rebuilt or changed for that test.
