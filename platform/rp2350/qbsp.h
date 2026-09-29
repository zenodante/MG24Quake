#ifndef QRP_QBSP_H
#define QRP_QBSP_H
#include "qpak.h"
/* Original Quake BSP29 disk layout, independent of MG24 converted structures.
 * Views borrow immutable QPAK memory. No heap, Flash writes or whole-map copy.
 * Callers own decompression scratch and PVS output; do not share mutable scratch
 * between cores. Successful open validates record ranges and references, not
 * arbitrary graph acyclicity; traversal has a hard node-count bound. */
enum { QBSP_ENTITIES, QBSP_PLANES, QBSP_TEXTURES, QBSP_VERTICES,
       QBSP_VISIBILITY, QBSP_NODES, QBSP_TEXINFO, QBSP_FACES,
       QBSP_LIGHTING, QBSP_CLIPNODES, QBSP_LEAVES, QBSP_MARKSURFACES,
       QBSP_EDGES, QBSP_SURFEDGES, QBSP_MODELS, QBSP_LUMPS };
typedef struct {
    uint32_t offset, size, count, stride;
    const uint8_t *mapped;
} qbsp_lump_t;
typedef struct {
    const qpak_t *pak;
    qpak_file_t file;
    qbsp_lump_t lump[QBSP_LUMPS];
    uint32_t textures, visleaves;
    bool runtime;
} qbsp_t;
/* Failure clears output. cache is temporary scratch, not retained. */
bool qbsp_open(qbsp_t *bsp, const qpak_t *pak, const char *name, qpak_cache_t *cache);
bool qbsp_read(const qbsp_t *bsp, unsigned lump, uint32_t offset,
               void *out, size_t size, qpak_cache_t *cache);
/* Bounded raw disk record. Decode little-endian fields; never cast to runtime
 * model structures or assume the mapped address is suitably aligned. */
const uint8_t *qbsp_record(const qbsp_t *bsp, unsigned lump, uint32_t index);
bool qbsp_point_leaf(const qbsp_t *bsp, uint32_t model, const float point[3], uint32_t *leaf);
/* hull 0 traverses the render BSP; hulls 1..3 traverse clipnodes. This is point
 * classification in a hull, not a swept collision trace. Points are in model
 * coordinates; callers must transform moving/rotating brush entities first. */
bool qbsp_point_contents(const qbsp_t *bsp, uint32_t model, unsigned hull,
                         const float point[3], int32_t *contents);
size_t qbsp_pvs_bytes(const qbsp_t *bsp);
/* Leaf zero or visofs=-1 yields all visible. Bit zero denotes leaf one.
 * On failure output is unspecified and must not be used. */
bool qbsp_leaf_pvs(const qbsp_t *bsp, uint32_t leaf, uint8_t *out, size_t capacity);
/* Read one original mip texture without expanding a complete skin/map.
 * Missing (-1) texture entries return false. */
bool qbsp_texture_mip(const qbsp_t *bsp, uint32_t texture, unsigned mip,
                       uint32_t *offset, uint32_t *width, uint32_t *height,
                       qpak_cache_t *cache);
/* Direct QLV1/TEX1 view, no texture copies or legacy relative-pointer patch. */
const uint8_t *qbsp_texture_pixels(const qbsp_t *b, uint32_t texture, unsigned mip,
                                 uint32_t *width, uint32_t *height);
bool qbsp_face_record(const qbsp_t *b,uint32_t index,uint8_t original[20]);
int32_t qbsp_surfedge(const qbsp_t *b,uint32_t index);
#endif
