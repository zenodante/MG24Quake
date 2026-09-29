#ifndef QRP_NATIVE_RESOURCE_FORMAT_H
#define QRP_NATIVE_RESOURCE_FORMAT_H
/* Included after quakedef.h. Produced with the same ARM compiler and config. */
#define QRN_VERSION 1u
/* Compile-time layout fingerprint, shared by resource linker and firmware. */
#define QRN_ABI (0x23500001u ^ (sizeof(model_t)*0x45d9f3bu) ^ (sizeof(brush_model_data_t)*0x119de1f3u) ^ (sizeof(mplane_t)*7919u) ^ (sizeof(mnode_t)*104729u) ^ (sizeof(mleaf_t)*15485863u) ^ (sizeof(texture_t)*32452843u) ^ (sizeof(msurface_t)*49979687u) ^ (sizeof(mtexinfo_t)*67867967u) ^ (offsetof(brush_model_data_t,textures)*86028121u) ^ (offsetof(msurface_t,samples)*104395301u) ^ (offsetof(texture_t,extmemdata)*122949829u) ^ (sizeof(mdl_t)*141650939u) ^ (sizeof(maliasskindesc_t)*160481183u))
#define QRN_BRUSH_MAGIC 0x314e4251u
#define QRN_ALIAS_MAGIC 0x314e4151u
#define QRN_SPRITE_MAGIC 0x314e5351u
typedef struct { char name[56]; const byte *data; uint32_t size,kind; } qrn_file_t;
typedef struct { uint32_t magic,count; const model_t *models; } qrn_brush_t;
typedef struct {
    char magic[4]; uint32_t version,size,crc,abi,count;
    const qrn_file_t *files; uint32_t base;
} qrn_header_t;
_Static_assert(sizeof(void*)==4,"ARM resources require 32-bit pointers");
_Static_assert(sizeof(model_t)==28 && sizeof(brush_model_data_t)==224,"native model ABI changed");
_Static_assert(sizeof(mplane_t)==16 && sizeof(mnode_t)==28 && sizeof(mleaf_t)==28,"native BSP ABI changed");
_Static_assert(sizeof(msurface_t)==32 && sizeof(mtexinfo_t)==40 && sizeof(texture_t)==36,"native surface ABI changed");
#endif
