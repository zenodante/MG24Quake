# RP2350 integration decisions

Reference: [next-hack, original MG24 Quake port, 2024-09-22](https://next-hack.com/index.php/2024/09/22/quake-port-to-sparkfun-and-arduino-nano-matter-boards-using-only-276-kb-ram/).
Consulted 2026-09-26 alongside this checkout. These are integration decisions,
not claims that the remaining engine work is implemented.

## Lessons from the author

The port combines buffer lifetime reuse, compact entities, generated native
QuakeC, texture grouping and bounded caches. Its converted assets, precomputed
skies and level-time internal-Flash caching are integral to its implementation.
These choices must be reviewed together with their hardware and storage costs.

## Decisions for this target

- Preserve the user-validated qservice/qmix and board drivers, including the
  two 320-pixel LCD row buffers. Engine integration must use their ownership API.
- Keep the existing QPAK v1 bytes and fixed resource offset. Normal firmware
  builds must not repack resources. Any future format migration is a separate,
  explicitly identified resource update.
- Retain the generated gameplay implementation and compact entity accessors.
  Audit short-pointer arena bounds and alignment before reusing the allocator;
  do not expand every stored index into a native pointer by default.
- Prioritize replacing model.c's storeToInternalFlash calls and converted-format
  assumptions. Read original BSP29/MDL data through bounded readers, map only
  genuinely contiguous uncompressed ranges, and budget derived mutable state.
  A Sys_File bridge alone does not complete this work.
- Do not introduce level-load resource Flash writes. Prefer existing XIP data
  and bounded RAM metadata. Prove each disk/runtime structure layout before
  attempting zero-copy access; raw disk bytes are not runtime model structures.
- Keep original sky and skin resources. Implement runtime sky composition and
  original skin sampling with bounded caches, measuring their costs before
  considering changes to the resource image.
- Audit r_main.c's Z-buffer reuse and d_surf.c's texture-buffer lifetimes against
  asynchronous core-1 display ownership. Reuse is allowed only after the last
  consumer finishes. Do not transplant MG24 DMA completion assumptions.
- Preserve the current full indexed frame initially. Measure total peak RAM,
  including Z-buffer, scratch buffers, entity arena and both core stacks, before
  adopting further overlap or partial refresh optimizations.
- Validate original-format loading first, then a rendered world, demo playback,
  and live gameplay. Record frame times, memory high-water marks and audio
  underruns; the hardware diagnostic is not a playable-engine milestone.

Code checkpoints: QuakeMG24/Quake/model.c, r_main.c, d_surf.c, r_sky.c;
platform/rp2350/qfiles.c, qpak.c, qservice.c and flash_layout.h.

## Implemented checkpoint: original BSP29 access

qbsp.c now provides validated immutable map views, bounded mip reads, point
classification and leaf PVS decoding. The diagnostic opens start.bsp. All 21
BSP resources pass host comparison; renderer integration remains the next step.

## Implemented checkpoint: static world reference renderer

qrender.c draws original BSP world geometry with clipping, PVS, reciprocal
depth, perspective textures and static lightmaps. It is a correctness/reference
path, not the optimized MG24 renderer or a completed game. Next integrations
remain alias/brush entities, animated effects, engine state and demo/gameplay.
