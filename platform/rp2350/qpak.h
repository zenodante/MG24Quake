#ifndef QPAK_H
#define QPAK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { QPAK_BLOCK_BYTES = 4096, QPAK_ASSET_OFFSET = 0x100000,
       QPAK_ASSET_CAPACITY = 0xec0000, QPAK_SAVE_OFFSET = 0xfc0000 };
typedef struct {
    const uint8_t *image;
    uint32_t bytes, files, blocks, directory, block_table, payload;
} qpak_t;
typedef struct {
    uint32_t size, first_block, block_count, crc32;
} qpak_file_t;
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
/* NULL if any byte is compressed or physically noncontiguous. */
const uint8_t *qpak_map(const qpak_t *pak, const qpak_file_t *file,
                        uint32_t offset, size_t length);
uint32_t qpak_crc32(const void *data, size_t length);
bool qpak_lz4_decode(const uint8_t *src, size_t size, uint8_t *dst, size_t expected);
#endif
