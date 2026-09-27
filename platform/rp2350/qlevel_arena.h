#ifndef QRP2350_LEVEL_ARENA_H
#define QRP2350_LEVEL_ARENA_H

#include <stddef.h>

/* RP2350 replacement for the MG24 internal-flash runtime-object store.
 * Immutable game assets stay in QXIP/XIP.  Only objects that the MG24 engine
 * actually constructs at load time are copied here and live until the level
 * arena is reset.
 */
#ifndef QRP_LEVEL_ARENA_BYTES
#define QRP_LEVEL_ARENA_BYTES (192u * 1024u)
#endif

size_t qlevel_arena_used(void);
size_t qlevel_arena_capacity(void);
size_t qlevel_arena_common_bytes(void);

#endif
