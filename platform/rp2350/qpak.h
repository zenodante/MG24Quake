#ifndef QPAK_H
#define QPAK_H
#include "flash_layout.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { QPAK_BLOCK_BYTES = 4096, QPAK_ASSET_OFFSET = QRP_ASSET_OFFSET,
       QPAK_ASSET_CAPACITY = QRP_SAVE_OFFSET - QRP_ASSET_OFFSET,
       QPAK_SAVE_OFFSET = QRP_SAVE_OFFSET,
       QXIP_BSP_LUMPS = 15 };
typedef enum { QPAK_FORMAT_NONE=0, QPAK_FORMAT_BLOCKS=1, QPAK_FORMAT_QXIP1=2 } qpak_format_t;
typedef struct {
    const uint8_t *image;
    uint32_t bytes, files, blocks, directory, block_table, payload;
    uint32_t strings, texture_store, texture_store_bytes, texture_count;
    qpak_format_t format;
} qpak_t;
typedef struct {
    uint32_t size, first_block, block_count, crc32;
    uint32_t direct_offset, kind;
    bool direct;
} qpak_file_t;
typedef struct {
    const uint8_t *base;
    uint32_t bytes;
    uint32_t texture_count;
    uint32_t lump_offset[QXIP_BSP_LUMPS];
    uint32_t lump_size[QXIP_BSP_LUMPS];
} qpak_level_t;
/* A cache belongs to one consumer/core. Never share a mutable cache. */
typedef struct {
    const qpak_t *owner;
    uint32_t block;
    bool valid;
    uint8_t data[QPAK_BLOCK_BYTES];
} qpak_cache_t;
bool qpak_open(qpak_t *pak, const void *image, size_t available);
bool qpak_find(const qpak_t *pak, const char *name, qpak_file_t *file);
bool qpak_read(const qpak_t *pak, const qpak_file_t *file, qpak_cache_t *cache,
               uint32_t offset, void *output, size_t length);
/* Returns an immutable XIP pointer when the requested bytes are contiguous. */
const uint8_t *qpak_map(const qpak_t *pak, const qpak_file_t *file,
                        uint32_t offset, size_t length);
/* QXIP1 level access. Non-texture lumps are immutable native BSP bytes. Lump 2
 * is a u32 global-texture-ID array rather than a Quake dmiptexlump_t. */
bool qpak_level_open(const qpak_t *pak,const qpak_file_t *file,qpak_level_t *level);
const uint8_t *qpak_level_lump(const qpak_level_t *level,unsigned lump,size_t *bytes);
bool qpak_level_texture_id(const qpak_level_t *level,unsigned index,uint32_t *texture_id);
/* Resolve a content-deduplicated global miptex record directly in XIP. */
const uint8_t *qpak_texture(const qpak_t *pak,uint32_t texture_id,size_t *bytes);
uint32_t qpak_crc32(const void *data, size_t length);
bool qpak_lz4_decode(const uint8_t *src, size_t size, uint8_t *dst, size_t expected);
#endif
