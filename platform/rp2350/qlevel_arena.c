#include "qlevel_arena.h"

#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"

/*
 * MG24 used otherwise-unused internal program flash as a bump allocator for
 * model/runtime objects that must survive after temporary load buffers are
 * released.  RP2350 has substantially more SRAM and must not program flash at
 * level load, so preserve the lifetime/ABI while changing the storage class.
 */
static uint8_t arena[QRP_LEVEL_ARENA_BYTES] __attribute__((aligned(4)));
static size_t cursor;
static size_t common_end;

static size_t align4(size_t n) {
    return (n + 3u) & ~3u;
}

static void *alloc_bytes(size_t size) {
    size_t n = align4(size);
    if (n > sizeof arena - cursor)
        panic("RP2350 level arena overflow: used=%u request=%u capacity=%u",
              (unsigned)cursor, (unsigned)n, (unsigned)sizeof arena);
    void *p = arena + cursor;
    cursor += n;
    return p;
}

void internalFlashInit(void) {
    cursor = 0;
    common_end = 0;
}

void internalFlashSetCommonZone(void) {
    common_end = cursor;
}

void internalFlashResetToCommonZoneEnd(void) {
    cursor = common_end;
}

void eraseInternalFlash(int sections) {
    (void)sections;
    /* Deliberately no flash operation.  Arena lifetime is controlled by the
       common-zone marker and reset operation. */
}

void *getCurrentInternalFlashPtr(void) {
    return arena + cursor;
}

void *reserveInternalFlashSize(int size) {
    if (size < 0)
        panic("RP2350 level arena negative reservation");
    return alloc_bytes((size_t)size);
}

void *storeToInternalFlash(const void *buffer, int size) {
    if (size < 0)
        panic("RP2350 level arena negative store");
    void *p = alloc_bytes((size_t)size);
    if (size)
        memcpy(p, buffer, (size_t)size);
    return p;
}

void *storeToInternalFlashAtPointer(void *buffer, void *position, int size) {
    if (size < 0)
        panic("RP2350 level arena negative fixed store");
    uintptr_t begin = (uintptr_t)arena;
    uintptr_t end = begin + sizeof arena;
    uintptr_t dst = (uintptr_t)position;
    if (dst < begin || (size_t)size > end - dst)
        panic("RP2350 fixed level-arena store outside arena");
    if (size)
        memcpy(position, buffer, (size_t)size);
    return position;
}

int getInternalFlashRemaningSize(void) {
    return (int)(sizeof arena - cursor);
}

size_t qlevel_arena_used(void) {
    return cursor;
}

size_t qlevel_arena_capacity(void) {
    return sizeof arena;
}

size_t qlevel_arena_common_bytes(void) {
    return common_end;
}
