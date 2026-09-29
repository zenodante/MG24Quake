# 768 KiB partition and immutable runtime-data audit

> Historical analysis preceding the complete firmware. Statements about an unfinished binder or diagnostic firmware sizes describe that stage, not current implementation. See [current results](RP2350_FULL_FIRMWARE_RESULTS.md) and [build instructions](../platform/rp2350/game/README.md).

Date: 2026-09-28. Conclusions use source inspection, ARM GCC type layouts and allocation traces from 3,240 Host_Frame calls across nine maps in the Mac full engine. Here, moving data to Flash means generating immutable records offline and including them in the resource image, not programming Flash at runtime.

## Findings

Alias/sprite descriptors, WAD/UI indices, model/sound entries and per-level resource maps should be generated offline. However, most zone-heap storage is already mutable game state. Moving the whole heap to Flash cannot provide another large SRAM reduction. Distinguish read-only loader products from truly mutable allocations.

No size-audit ELF was packaged as executable firmware during this audit. The buildable RP2350 target at that time was quake_rp2350_bringup: world view, collision, display, input and audio diagnostics without the complete game. It used the Pimoroni Explorer ST7789/QwSTPad configuration and had not been tested on hardware. The complete engine's native binder and SRAM layout were still unfinished.

## Artifacts and addresses at the audit date

| Region | Half-open address range | Capacity |
|---|---|---:|
| Usable firmware | 0x10000000–0x100BF000 | 782,336 B |
| Update guard | 0x100BF000–0x100C0000 | 4,096 B |
| Resources | 0x100C0000–0x11000000 | 15,990,784 B (15.25 MiB) |

The firmware reservation totals 768 KiB; there is no separate save partition. Layout ABI is v3. Migration from the old 1 MiB boundary requires matching firmware and resource placement; updating only one produces a mismatch.

The audit resource UF2 contained QXIP3/QLV1, TEX1, LMAP, IDPX alias models and QAD1 audio: 339 files, 21 BSPs and 396 deduplicated textures. Payload was 15,826,560 B with 164,224 B spare. After 256-byte UF2 padding, the programmed end was 0x10FD7F00, leaving 164,096 B.

Diagnostic firmware bin: 78,212 B; firmware UF2: 157,184 B; resource UF2: 31,653,376 B. UF2 is a transport container, not Flash usage. Checks covered family ID, numbering, addresses, guard, disjoint writes and byte reconstruction; old 1 MiB addresses and incorrect family/block counts were rejected. These checks did not establish hardware operation.

## Mutable zone heap: peak 53,864 B

The `build_game.py --memory-audit` observer records every Z_Malloc2/Z_Free. Each new peak saves the allocations alive at that instant, avoiding addition of unrelated peaks from different maps or times. All nine maps reached 357 frames at signon 4; firing, damage and ADPCM checks passed.

| Allocation source at peak | Bytes including block headers | Blocks | Flash suitability |
|---|---:|---:|---|
| ED_Alloc | 51,640 | 311 | Whole entities are mutable; inspect constant fields by class |
| SV_ClearWorld | 1,768 | 1 | Spatial axes/planes/children may be constant; entity lists must remain writable |
| CL_ParseServerInfo | 408 | 1 | nodeHadDlight is cleared/updated each frame; keep in SRAM |
| Zone management header | 48 | — | Writable allocator state |

Entities account for about 95.9%. Position, velocity, health, ammo, animation, AI targets, nextthink, entity links and area links change. Doors/items can change through triggers, pickups, respawns or death even if initially stationary. Origin in BSP entities does not make an entire entity const. Relevant code includes sv_phys.c, sv_main.c, quakeProgs.c and entity_getters_setters.h.

The areanodes in world.c would need separate immutable spatial trees and mutable trigger/solid list heads. SV_UnlinkEdict/SV_LinkEdict update the lists during movement. Potential savings are only around a kilobyte, so priority is low. R_PushDlights/R_MarkLights in r_light.c clear/set nodeHadDlight, which is a necessary mutable sidecar.

### Splitting entity fields

Some fields may be moved, but validation must consider class and lifetime rather than names alone. Candidates include path_corner position/target names, some decorations and trigger configuration. Model, frame, think, use and spawnflags all have runtime assignment paths and cannot be globally frozen. Dynamically spawned projectiles, backpacks and debris still need RAM representations; save/load cannot depend on temporary pointers.

MG24 already separates static light romEntvars from mutable entity shells. Those records could be generated in the package instead of using storeToInternalFlash at runtime. For ordinary entities, moving only one or two 16-bit fields may be offset by an extra 32-bit descriptor pointer and alignment. Calculate net savings per class first.

These are 64-bit Mac heap observations, not RP2350 measurements. ARM GCC layouts: memblock_t 16 B (Mac: 32 B), monster_edict_t 212 B, player_edict_t 312 B, func_edict_t 216 B, trigger_edict_t 116 B, path_corner_edict_t 32 B. The 311 Mac entity allocations consume 9,952 B in block headers alone. Grouping allocations by entity class could reduce headers and fragmentation if SRAM remains constrained. That is a RAM allocator optimization, not Flash conversion, and does not require compressed pointers.

## Legacy loader products: peak live records 63,936 B

These records were written into the Mac internalFlash simulation array and are **not included in the zone peak above**. The array high-water pointer was 65,768 B including alignment and common-zone page gaps; actual live records totaled 63,936 B. Temporary construction buffers enter the zone only while loading and are then freed.

| Source | Bytes | Offline approach and constraints | Priority |
|---|---:|---|---|
| Mod_LoadAliasModelMemoryReady | 24,544 | Generate mdl/stvert/skin/frame/group descriptors; triangles, frame vertices and skins stay in XIP; frame selection remains mutable | High |
| Draw_Init | 16,384 | conchars pixels already exist in gfx.wad; reference them directly under memory mapping | High |
| finalizeModKnown | 8,192 | Fixed native descriptors plus mutable needload/cache-invalidity sidecars; do not freeze mod_known unchanged | High |
| W_LoadWadFile | 5,216 | Normalize names and expand the directory offline, replacing rather than duplicating it | High |
| CL_ParseServerInfo | 5,112 | Fixed per-level model/sound maps need shared server/client numbering and handling of demos/game modes | High; requires numbering design |
| SV_SpawnServer | 2,048 | Fixed model precache names/IDs; do not assume startup traversal order without validation | Same |
| Sbar_Init | 1,080 | Generate HUD pointer tables referencing existing WAD pixels | High |
| qcc_makestatic | 960 | Generate romEntvars for static lights/decorations; preserve shells/visibility state and account for skill/mode filtering | Medium |
| Mod_LoadSpriteFrame / Mod_LoadSpriteModel | 336 | Fixed dimensions/origins/pixel pointers; selected frame and entity position remain mutable | High |
| R_InitTextures | 64 | Compile missing-texture descriptors as const or include in package | Easy, small saving |

Mod_ClearAll changes needload and clears sprite data. Directly placing existing model_t arrays in read-only Flash would therefore fail on level transitions. Separate fixed descriptors from mutable selection/loading state instead of merely replacing malloc with const.

Precache maps are more complex than BSP local texture → global TEX1 maps. Texture mappings derive from files, whereas model/sound sets and order derive from spawn/game logic and may vary with skill, deathmatch, coop and demos. Generate verifiable per-level manifests or configuration variants, share IDs between server/client, and reject unknown requests instead of silently assigning incorrect resources.

## SRAM outside the heap

The game_sfx[MAX_SFX] static array in game_sound.c is 255×44 = 11,220 B under ARM. Names, data addresses, lengths and loop points could become a global read-only table with a registration bitmap/small sidecar. Playback position, gain and ADPCM predictor/index must stay mutable; do not freeze realtime mixer channels with fixed sound descriptors.

Framebuffer, Z-buffer, edge/span workspaces, lighting workspaces, particles, messages, input, mixer and stacks are computation/communication storage, not missing offline resource conversion.

The observer itself adds 274,472 B of static bookkeeping and host stack calls. It is excluded from production; its RSS/stack high-water mark is not a target budget. The 15 MiB/64 MiB host simulated Flash arrays belong to legacy tooling and must be removed from target SRAM.

## Resource capacity still required verification

The audit UF2 held QXIP, not the Mac 64-bit QNAT relocation package or a final all-native RP2350 image. Conservative ARM ABI replacement estimates were:

- Replace selected QLV structures and add all model/brush entries: 15,868,332 B.
- Add hull0: 16,000,020 B, exceeding the 15.25 MiB partition by 9,236 B.
- The estimate still retained 635×64 = 40,640 B of original dmodels. Once every consumer used native descriptors, this duplicate could theoretically be removed. It could not be removed while the diagnostic QLV reader still needed it.

Do not simply append 63.9 KB of metadata to the existing image. Replace old descriptors, reuse pixels and remove duplicate tables, then recalculate with the target ABI. Most alias source/runtime records can replace one another; font/WAD pixels need not be duplicated. Final fit requires an actual generated container and binder, not inference from the 164 KB QXIP margin.

Recommended order at the time: complete the native brush binder and nonduplicated container; move alias/sprite/WAD/HUD/maps offline; then use real ARM RAM peaks to decide whether finer entity splitting is worthwhile. Remove demonstrably constant loading products rather than sacrificing mutable gameplay state to reduce heap numbers.

## Reproduce allocation analysis

```sh
python3 platform/macos/build_game.py --memory-audit -o build-host/memory-audit
python3 platform/macos/test_game.py \
  --player build-host/memory-audit/quake_game \
  --assets build-host/mac-game-resources/quake-resources.qres \
  --output build-host/release-768k/memory-audit
```

Raw reports contain zone_peak_allocations and legacy_flash_peak_allocations, each representing its own simultaneously live peak. This audit did not itself freeze new fields or claim that host read/write paths were a completed hardware XIP binder.
