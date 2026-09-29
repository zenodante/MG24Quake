# Host-generated immutable resource images

> Current RP2350 firmware uses the final QRN1 fixed-pointer image, generated from QXIP3 by `--resource-profile rp2350-game`. The current layout is 768 KiB program reservation (including guard) plus 15.25 MiB assets at `0x100C0000`. Earlier capacity figures below are historical format measurements. See [full firmware](../platform/rp2350/game/README.md).


This document is the host-tool contract, based on the local MG24 implementation.
No RP2350 engine, renderer or driver changes are included in this stage.

## Mapping after deduplication

A resource ID is a serialized relationship, not a compressed SRAM pointer.
QXIP stores no native pointers or fixed flash addresses. Files are indexed by
the QXIP directory; exact miptex records (including name, dimensions and all
four mip levels) are deduplicated into TEX1. Deduplication does not combine
textures merely because their names match, nor merge different names merely
because their pixels match: names affect sky/water/animation semantics.

Each level has a local texture ID -> global TEX1 ID array in LMAP. `0xffffffff`
means missing texture. `qlevel_assets.h` is generated from the same image and
contains image SHA-256, level file IDs, level offsets/sizes, mapping offsets,
section IDs, counts and a macro for every local -> global texture mapping.
Macros do not allocate arrays in firmware. The authoritative arrays remain in
XIP; generic consumers bind a descriptor, while specialized code can use the
header's constants. Header offsets are image-relative. Regenerate the header
whenever the image changes; IDs are deterministic for identical input/order,
but are not promised stable across changed input packages.

Texture animation is **level-local**. Texinfo references a local texture slot,
whose immutable descriptor references TEX1 and local next/alternate slots.
A global TEX1 record must not own animation links: the same texture can occur
in levels with different frame sets. Missing animation frames and ambiguous
duplicate frame names are rejected. Timing uses MG24's `ANIM_CYCLE=2` at 10 Hz.

## What the source actually loads and changes

| Resource | MG24 source / conversion | QLV1 decision |
|---|---|---|
| Planes | `model.c:Mod_LoadPlanes`, computes signbits | float normal/dist, type/signbits; 20 bytes, no duplicate disk planes |
| Texinfo | `Mod_LoadTexinfo`, Length thresholds, flags, texture binding | vecs, local texture slot, effective flags, mipadjust; 44 bytes |
| Faces | `Mod_LoadFaces`, `CalcSurfaceExtents` | firstedge, plane/texinfo IDs, edge count, static flags, texturemins/extents, styles/light offset, owner node; 32 bytes |
| Nodes/leaves | `Mod_LoadNodesAndLeafs`, `Mod_SetParent`; original converter supplies parent/owner relationships | retain BSP topology and append int32 parent; 28/32 bytes; leaf zero is shared solid and has parent -1 |
| Textures | `Mod_LoadTextures` builds animation links and mip pointers | global exact mip records + per-level descriptors; no address patching |
| Vertexes/edges | `Mod_LoadVertexes`, `Mod_LoadEdges` mostly copy | retain original immutable arrays |
| Marksurfaces | `Mod_LoadMarksurfaces` copies IDs | retain uint16 face IDs |
| Surfedges | `Mod_LoadSurfedges` narrows indices | signed int16 when EVERY index fits; otherwise int32, stride recorded |
| Clipnodes | `Mod_LoadClipnodes` copies plane/child relationships | retain original 8-byte BSP records |
| Hull0 | `Mod_MakeHull0` duplicates drawing tree | use nodes as hull0; negative child resolves leaf.contents; no extra array |
| Models | `Mod_LoadSubmodels`, bounds/radius branches in `Mod_LoadBrushModel` | retain 64-byte float bounds/origin/headnodes/face ranges; native bound/radius policy deferred |
| Lighting/PVS/entities | `Mod_LoadLighting/Visibility/Entities` | retain source bytes, PVS stays compressed |

The immutable surface flags use **this MG24 tree's** values: PLANEBACK=1,
DRAWSKY=2, DRAWTURB=8, DRAWTILED=16 (not upstream Quake's values).
Sky/turbulence classification comes from texture names. Turbulent surfaces get
texturemins=-8192 and extents=16384. Missing textures clear texinfo flags and
retain a sentinel requiring the consumer's checkerboard fallback.

`r_main.c:R_MarkLeaves` changes visibility state; MG24 configurations either
use visframe or separate visnodes/visleaves bitmaps. `r_light.c:R_MarkLights`
changes dynamic-light marks/bits, also sometimes in separate arrays.
`r_efrag.c` maintains entity/leaf links. `d_surf.c` maintains rendering caches.
These states are not serialized. Neither are model registry slots, entity
transforms, frame counters, texture cache pointers, sound channel/decoder state,
or pointers created by the loader. A surface's array index supplies its ID;
no model-registry index is baked into static map data.

Thus MG24's load-time allocation is **not** a measurement of required mutable
SRAM: much of it is temporary construction before `storeToInternalFlash()`.
The host report deliberately does not invent an SRAM budget; the later engine
must choose its visibility, lighting, efrag and cache implementations first.

Remaining work is explicit: alias MDL and QAD1 use their existing converters;
sprite descriptors, any additional alias loader relocation work, sky composition,
submodel radius/bounds policy, and generated gameplay string tables are not
newly converted by QLV1. Sky source pixels are immutable, while sky composition
has time-dependent output. Expanding all PVS rows or storing a duplicate hull0
is not justified by the current flash budget.

## Binary contract

All integers and IEEE binary32 values are little endian. All section starts
are 4-byte aligned. Consumers must decode fixed-width records, not cast them
to compiler-dependent engine structures or C bitfields.

QXIP3 uses the same 48-byte `<4s11I` header layout as QXIP2: magic, version,
file count, strings offset, directory offset, payload offset, TEX1 offset,
TEX1 size, texture count, LMAP offset, LMAP size, image size. Directory entries
are `<4I`: string-relative name offset, kind (0=file, 1=level), image-relative
payload offset, byte size. TEX1 and LMAP retain the QXIP2 layout.

A QXIP3 level payload is QLV1 instead of BSP29. Header `<4sIII>`: `QLV1`,
version=1, payload byte size, section count=15. Followed by 15 `<4I>` entries:
level-relative offset, byte size, record count, record stride. Section IDs
retain BSP lump numbering. Variable byte streams use stride=1.

| ID | Section | Python struct notation / stride |
|---|---|---|
| 0 | entities | bytes |
| 1 | planes | `<4fBBH`: normal/dist, type, signbits, reserved=0; 20 |
| 2 | local textures | `<6I`: global ID, next local, alternate local, total/min/max ticks; 24 |
| 3 | vertices | `<3f`; 12 |
| 4 | visibility | original compressed bytes |
| 5 | nodes | `<i8h2Hi`: BSP node fields + parent; 28 |
| 6 | texinfo | `<8fIiI`: vecs, local texture ID, effective flags, mipadjust; 44 |
| 7 | surfaces | `<I4H2h2H4Bii`: firstedge, plane, texinfo, numedges, flags, texturemins, extents, styles, light offset, owner node; 32 |
| 8 | lighting | bytes |
| 9 | clipnodes | `<i2h`; 8 |
| 10 | leaves | `<ii6h2H4Bi`: BSP leaf fields + parent; 32 |
| 11 | marksurfaces | `<H`; 2 |
| 12 | edges | `<2H`; 4 |
| 13 | surfedges | `<h` or `<i>` according to section stride |
| 14 | submodels | `<9f7i`; 64 |

Parent and owner -1 mean absent. Node child >=0 means node ID; negative means
leaf ID `-1-child`. Clipnode negative children are contents codes instead.
Light/visibility offset -1 means absent. Brush files with an empty visibility
lump normalize their unused leaf visibility offset to -1.

Topology is validated for references, cycles and conflicting parents (except
shared solid leaf zero). Source lump bounds/overlap, record lengths, finite
numbers, texture mip ranges, surface lighting ranges and PVS runs are checked.
`verify_xip.py` separately validates emitted structure, TEX1 hashes, LMAP IDs
and important runtime record references. This is format validation, not a proof
that arbitrary maps are compatible with every Quake gameplay/renderer limit.

## Reproduce on macOS

From the repository root:

```sh
python3 Tools/RP2350Pack/run_pipeline.py build/pak0.pak \
  -o build-host/preexpanded --firmware-bytes 0x100000
python3 Tools/RP2350Pack/verify_xip.py build-host/preexpanded/quake-assets.qxip
python3 Tools/RP2350Pack/tests/test_runtime.py build-host/preexpanded/pak0conv-python.pak
```

The default emits QXIP3, JSON section accounting and `qlevel_assets.h`.
`--resource-profile qxip-only --level-format bsp29` explicitly retains the old QXIP2 BSP compatibility path.
**Existing firmware does not consume QXIP3.** No firmware changes or flashing
are part of this host-tool work. UF2 host packagers now validate versions 1–3;
being packageable does not imply compatibility with the installed firmware.

Tests compare all source texinfo/surface math with an independent C transcription
of MG24's formulas, compiled with floating-point contraction disabled. Python
rounds intermediate operations to binary32. This establishes the documented
non-FMA math contract, not target-specific fast-math equivalence. Raw sections
and exact texture records are compared byte-for-byte; tests also exercise
animation isolation, malformed input, truncation, invalid IDs, hash corruption,
header compilation, determinism and the old QXIP2 path.

Measured original shareware pak0: 339 files, 21 BSPs (9 maps and 12 brush files),
586 local texture slots -> 396 unique textures; exact texture saving 1,205,380 B.
1,962 texinfo and 42,398 surfaces pass C math comparison.

QXIP3 is **15,424,320 B**, against the actual **15,466,496 B** asset partition:
**42,176 B** remain. The earlier 15 MiB report omitted the 256 KiB save reserve.
Surface expansion adds 508,776 B, node/leaf parents add 137,140 B, texinfo adds
7,848 B; surfedge narrowing saves 407,764 B. Budget failure is fatal before
writing a new image. Raw/expanded geometry is never stored twice in QLV1.


## Mac native ABI validation (QNAT1)

The pipeline now defaults to `--resource-profile mac`, builds the native tool
and runs `native_image.py` after QXIP validation. `--native-packer` overrides
the compiler path; `--resource-profile qxip-only` selects the legacy output. It emits `quake-assets-mac.qnat`,
`qnative_assets.h` and a native manifest. This is a **host-only** preexpanded
structure image: all pointer-bearing structures are built offline using the
same C ABI as the MG24 renderer. The player only relocates explicitly listed
pointer slots once at startup and makes the mapping read-only. Level changes
select model entries without rebuilding or allocating pointer metadata.

The generated header contains fixed per-model entry offsets and both artifact
hashes. QNAT also checks the paired QXIP CRC and ABI fingerprint. Mac ASLR is
handled by startup relocation; a future fixed-address target can apply the
corresponding relocation at image-generation time instead.

Current host fixture: 21 BSP entries, 4,734,504 bytes, 146,778 pointer
relocations, zero per-level metadata heap allocation. QNAT duplicates some QLV1
metadata for ABI validation; it must not be added to the hardware image or
interpreted as a finished target Flash budget. The final target representation
must replace corresponding source records and be regenerated for its 32-bit
ABI. See `platform/macos/README.md` for build, rendering tests and accounting.


## QRES1 complete Mac resource package

The default pipeline emits `quake-resources.qres`: one uncompressed file the
Mac player opens with `--assets`, requiring no separate `--native` argument.
The 64-byte little-endian header is `<4s15I>`: magic, version 1, total bytes,
target 1 (Mac native 64), QXIP offset/size, QNAT offset/size, both section CRC32s,
header CRC32 (computed with its own field zero), and five reserved zero words.
QXIP starts at 16384; QNAT starts at the next 16384-byte boundary after QXIP.
Padding is zero. QNAT consumes the remainder of the file; no trailing bytes.

The complete package validator checks both nested formats, their pairing,
all BSP entry names/order, hashes and exact size accounting. The reader maps
QNAT privately from its section offset, relocates once and protects it read-only.
No unpacking to temporary files or per-level pointer metadata is needed.

Measured complete size: 20,184,616 bytes = 15,424,320 QXIP + 4,734,504 QNAT +
64 header + 25,728 padding. It exceeds the 15 MiB asset partition
by 4,455,976 bytes. This host-ABI validation container is not a hardware image;
its native metadata includes 1,174,224 relocation bytes and duplicates some
QLV1 structures. No target-size success is inferred from QXIP alone.

`--require-flash-fit` rejects publication on overflow while retaining the prior
package and writing a size report. Default Mac mode permits the validation
package but records `package_fits_reference_capacity: false`. The generated
`qresource_package.h` exposes section entry offsets and package SHA256;
`resource-package.json` and `summary.json` include the full size result.

## ARM native ABI audit

`run_pipeline.py --target-abi-audit` additionally runs `target_abi.py` with
`../pico8c/third_party/toolchains/*/bin/arm-none-eabi-gcc`. Override the tree with
`--pico8c-root`. This compiles a data-only object for Cortex-M33, extracts sizes
and alignments, and confirms native 32-bit pointers with SRAM short pointers
disabled. It uses the portable C/native-pointer configuration of the Mac harness;
this is a proposed resource ABI, not an assertion about existing firmware.

The JSON explicitly reports a projection, not a linked target image. It replaces
planes, nodes, texinfo, surfaces, leaves and local texture records; it adds native
model/brush descriptors for **every** world and inline model, plus the immutable
collision nodes generated by MG24 `Mod_MakeHull0`. Remaining QXIP data is retained.
These collision nodes can be compiled offline: node plane numbers stay unchanged,
positive children are node indices, and negative leaf references become contents.
They do not need level-time SRAM or Flash writes.

With the current PAK, replacing arrays and including all model descriptors costs
15,466,092 bytes; hull0 adds 131,688 bytes. Total projection: **15,597,780 bytes**.
Under the user-confirmed 1 MiB program / 15 MiB asset budget, remaining space is
**130,860 bytes**. There is no separate save reservation. Final linker/container
padding, visibility representation, mutable model registry fields, and alias/sprite
runtime expansion still require implementation and measurement. Do not treat this
projection as a complete target-image fit test or a full-game memory measurement.
