#ifndef QRP_FLASH_LAYOUT_H
#define QRP_FLASH_LAYOUT_H
/* Persistent layout ABI v1. Never derive resource addresses from firmware size.
 * Existing QPAK bytes remain valid across firmware-only updates.
 * Changing these boundaries requires an explicit resource migration. */
#define QRP_XIP_BASE        0x10000000
#define QRP_FIRMWARE_BYTES  0x000ff000
#define QRP_GUARD_OFFSET    0x000ff000
#define QRP_ASSET_OFFSET    0x00100000
#define QRP_SAVE_OFFSET     0x00fc0000
#define QRP_FLASH_BYTES     0x01000000
#endif
