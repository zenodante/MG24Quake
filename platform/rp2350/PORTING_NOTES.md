# RP2350 Quake port plan

Reference: [next-hack, original MG24 Quake port, 2024-09-22](https://next-hack.com/index.php/2024/09/22/quake-port-to-sparkfun-and-arduino-nano-matter-boards-using-only-276-kb-ram/).

This document is the architectural plan for the RP2350 port.  The central rule
is: **MG24 is the engine/renderer baseline, not the RP2350 hardware baseline.**
Keep the author's Quake algorithm and performance work; replace hardware and
memory compromises that existed only because MG24 had 276 kB RAM and external
SPI storage.

## Fixed RP2350 architecture

The core-1 service architecture is already validated and is not part of the
renderer rewrite.

- Core 0 runs the Quake engine and the MG24-derived software renderer.
- There is one 320x200 8-bit indexed framebuffer (64,000 bytes).
- Core 1 owns display conversion/output, input polling, and audio mixing/output.
- Display conversion uses two independent 320-pixel RGB565 row buffers.  Core 1
  converts one framebuffer row at a time and ping-pongs the row buffers with
  DMA.  Do not reintroduce multi-row staging buffers or a second full frame.
- Core 1 polls the keyboard/input path at the validated rate and keeps the
  existing qservice/qmix/board-driver ownership model.
- Engine integration must adapt to qservice; do not transplant MG24 display,
  audio, input, or DMA scheduling into the RP2350 service core.

The single framebuffer has an ownership barrier: Core 0 may render only when
Core 1 is not consuming that frame.  A second full framebuffer is not the
solution to synchronization bugs.

## Renderer: discard the RP2350 reference renderer

`platform/rp2350/qrender.c` is a bring-up/reference rasterizer.  Its
bounding-box triangle walk, per-pixel barycentric floating point, per-pixel
perspective division and light interpolation are intentionally not the final
renderer.  It must not be optimized into a second Quake renderer.

The production path shall use the existing MG24 Quake rendering pipeline,
including the relevant code in:

- `r_main.c`, `r_bsp.c`, `r_edge.c`, `r_surf.c`, `r_sky.c`
- `d_edge.c`, `d_scan.c`, `d_surf.c`, and related sprite/alias paths
- the MG24 fixed-point, approximate-division, packed-operation and span-loop
  optimizations where they remain correct on Cortex-M33.

In particular, retain the edge/surface/span architecture and the optimized
`D_DrawSpans8` family.  Do not replace it with triangle fans or a new generic
rasterizer.

`qbsp.c`, `qcollision.c`, and `qrender.c` may remain temporarily as diagnostic
code while the engine is brought up.  They are not a parallel production
engine.  Once the corresponding MG24 model/collision/render paths are running,
the production target must stop compiling the redundant diagnostic path.

## What to preserve from MG24

Preserve optimizations whose purpose is CPU efficiency or compact hot runtime
state:

- BSP/PVS traversal and edge/span visibility pipeline;
- fixed-point hot loops and reduced division;
- Cortex-M packed arithmetic that is valid on RP2350 Cortex-M33;
- generated/native QuakeC and compact gameplay/entity representation;
- mip selection, span drawing, surface/light processing and other measured
  renderer optimizations;
- precomputation that removes expensive per-frame work when its storage cost is
  reasonable on the RP2350.

Do not silently revert an MG24 optimization to original desktop Quake merely
because RP2350 has more RAM.  First identify whether the optimization was for
speed or only for memory/storage pressure.

## What to change for RP2350

RP2350 has 520 kB SRAM, two cores, and a 16 MiB memory-mapped XIP flash.  Use
those differences deliberately.

- Spend SRAM to reduce CPU/XIP traffic: renderer working sets, hot lookup tables,
  surface/texture cache and mutable model state are candidates.
- Keep dynamic/hot/random-access structures in SRAM when profiling shows XIP
  cache misses are material.
- Keep immutable bulk resources in XIP when their on-disk representation can be
  consumed safely and efficiently.
- Do not preserve MG24 RAM aliasing/reuse merely to save a few kB if it creates
  lifetime coupling with asynchronous Core-1 display ownership.
- Do not preserve MG24 external-SPI-flash interleaving or EFR32-specific DMA,
  display, input, audio, timing, or flash code.

A proposed RAM budget must be checked against the linker map and measured peak
runtime use.  The 64 kB framebuffer is fixed; the remaining SRAM should be used
as a performance resource rather than left artificially constrained to MG24's
276 kB design point.

## XIP resource strategy

The 16 MiB flash is a persistent immutable resource store.  **Changing levels
must never rewrite/copy a level into a second internal-flash cache.**  All
resource transformation that is independent of gameplay happens on the host.

Preferred runtime hierarchy:

1. direct pointer into contiguous raw XIP data when layout/alignment permit;
2. bounded XIP reader for immutable data that cannot safely be cast;
3. SRAM cache/copy for hot or mutable data;
4. host-side derived representation when it measurably removes expensive
   runtime work.

Never treat raw BSP/MDL disk structures as native runtime structures without
proving size, packing, alignment, endianness and pointer/index representation.

### Host tools audit

The repository currently contains two different resource approaches:

- `Tools/MCUPackConverter`: the original MG24 converter.  It already performs
  useful engine-specific transformations (alias triangle data, prerendered sky,
  BSP modification, sound resampling, string generation), but it also embodies
  assumptions made for MG24 flash caching and storage.  Reuse transformations
  individually after auditing their consumers; do not adopt the whole format
  blindly.
- `Tools/RP2350Pack/pack.py`: a lossless paged QRP2350 container.  It preserves
  original files but may LZ4-compress texture/lightmap blocks.  This is useful
  for compatibility/diagnostics, but compressed pages are not direct XIP and
  therefore are not the production hot-resource format by default.

`Tools/RP2350Pack/prepare_xip.py` is the new minimal production staging tool. It
merges `pak0.pak` and optional `pak1.pak` with later-PAK override semantics and
emits an uncompressed 4-byte-aligned conventional PAK.  This provides a clean
immutable XIP baseline and fixes the missing host-side retail PAK merge without
introducing level-time flash writes.

The tool intentionally does not yet duplicate MG24 preprocessing.  During
engine integration, each MG24 converted-format dependency must be classified:

- **speed transformation**: port it into an RP2350 host converter and keep the
  derived bytes in XIP;
- **RAM workaround**: normally remove it and use RP2350 SRAM instead;
- **external-flash workaround**: remove it;
- **format convenience only**: choose whichever representation gives simpler
  bounded XIP access without slowing hot paths.

This classification prevents both extremes: blindly retaining all MG24 flash
machinery, or throwing away the author's performance work as the old
`qrender.c` path did.

## Migration phases

### Phase 0 - freeze validated platform services

Treat qservice/qmix, ST7789 DMA, one-line RGB565 ping-pong conversion, input and
audio on Core 1 as the known-good platform layer.  Only change these when a
measured integration defect requires it.

### Phase 1 - build the real MG24 engine on RP2350

Create/convert the RP2350 production target so it compiles the MG24 Quake engine
and renderer instead of `qrender.c`.  Add a thin RP2350 HAL for time, file/XIP,
input events, audio submission and frame ownership.  Resolve EFR32 dependencies
at the HAL boundary rather than forking renderer algorithms.

First milestone: engine initialization and loading `start` using immutable XIP
resources, with no level-time flash programming.

### Phase 2 - restore MG24 world rendering

Bring up BSP/PVS -> edge -> surface -> span rendering and write directly into
the single indexed framebuffer.  Preserve the MG24 optimized span loops.  Use
Core 1 unchanged to display completed frames.

First visual milestone: `start` world rendered through the MG24 renderer, not
`qrender.c`.  Record core-0 render time independently of LCD transfer time.

### Phase 3 - model/collision/game integration

Use the MG24/Quake model and hull structures and normal engine collision path.
Retire `qbsp/qcollision` from the production target as their replacements become
functional.  Restore brush entities, alias models, sprites, particles, sky,
water/turbulence, dynamic lighting and gameplay incrementally.

### Phase 4 - host conversion for proven hot formats

Audit every place where the MG24 engine expects MCUPackConverter output.  Port
only the speed-relevant transformations into RP2350 host tooling.  Derived data
is generated once on the PC and stored directly in the XIP resource image.
There is no runtime flash cache population.

### Phase 5 - use extra SRAM for speed

After the real renderer works, measure linker RAM, runtime high-water mark, XIP
miss-sensitive paths and frame breakdown.  Increase surface/texture/hot-data
caches within a documented SRAM budget.  Prefer measured cache enlargement over
new arithmetic micro-optimizations.

### Phase 6 - RP2350-specific CPU optimization

Only after profiling the MG24 baseline, tune Cortex-M33-specific hot loops,
compiler options, SRAM placement and XIP access patterns.  Keep before/after
frame-time measurements.  Do not optimize display-core code to fix a Core-0
renderer bottleneck.

## Performance instrumentation

For each playable milestone record at least:

- total Core-0 frame time;
- world edge/BSP time, surface setup time and span/raster time where practical;
- number of visible surfaces/edges/spans or equivalent renderer counters;
- XIP bytes/cache/refill proxies where measurable;
- Core-1 displayed frames and display queue stalls;
- audio underruns;
- peak SRAM/high-water marks.

The existing 3-5 FPS diagnostic is a measurement of the temporary reference
renderer, not a target baseline for the MG24 renderer.

## Completion criteria

The migration is complete when the production RP2350 target:

1. runs the MG24-derived Quake engine/renderer rather than `qrender.c`;
2. uses the validated Core-1 display/input/audio service unchanged in structure;
3. renders through one 320x200 indexed framebuffer and two one-row RGB565 DMA
   buffers;
4. loads immutable resources from the 16 MiB XIP image without level-change
   flash writes;
5. retains speed-relevant MG24 preprocessing/renderer optimizations;
6. uses RP2350's additional SRAM as measured cache/working memory;
7. runs world, entities, collision, audio/input and demo/gameplay with profiling
   counters sufficient to guide subsequent optimization.
