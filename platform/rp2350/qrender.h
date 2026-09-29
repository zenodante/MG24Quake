#ifndef QRP_QRENDER_H
#define QRP_QRENDER_H
#include "qbsp.h"
#ifdef QR_XIP_ONLY
#define QR_FACE_LIMIT 8192
#else
#define QR_FACE_LIMIT 65536
#endif
enum { QR_WIDTH=320, QR_HEIGHT=152, QR_MAX_FACES=QR_FACE_LIMIT, QR_MAX_LEAVES=8192,
       QR_TEXTURE_BYTES=32768, QR_POLY_VERTS=72 };
typedef struct { float position[3], yaw; } qr_camera_t;
typedef struct { float x,y,z,s,t; } qr_vertex_t;
typedef struct { unsigned faces,triangles,pixels,rejected; } qr_stats_t;
/* Core-0-owned reference world renderer. The 16-bit depth buffer covers only
 * the 3D viewport. Full frame ownership stays with qservice. No Flash writes. */
typedef struct {
    uint16_t depth[QR_WIDTH*QR_HEIGHT];
    #ifndef QR_XIP_ONLY
    uint8_t texture[QR_TEXTURE_BYTES], colormap[64*256];
#endif
    const uint8_t *texture_pixels, *color_rows;
    uint8_t visible[QR_MAX_FACES/8], pvs[QR_MAX_LEAVES/8];
    uint8_t light[4*18*18];
    qr_vertex_t poly[2][QR_POLY_VERTS];
#ifndef QR_XIP_ONLY
    qpak_cache_t cache;
#endif
    const qbsp_t *world;
    int texture_id;
    unsigned mip,tw,th;
    bool ready;
    qr_stats_t stats;
} qr_renderer_t;
bool qr_init(qr_renderer_t *r,const qbsp_t *world);
bool qr_spawn_camera(const qbsp_t *world,qr_camera_t *camera);
/* Renders world model zero, static lighting, perspective-correct textures.
 * No alias entities, moving submodels, sky animation, water warp or lightstyles.
 * Output is QR_WIDTH*QR_HEIGHT bytes; caller supplies any status area separately.
 * false indicates invalid view/data or unsupported limits, not a complete frame. */
bool qr_draw(qr_renderer_t *r,const qr_camera_t *camera,uint8_t *pixels);
#endif
