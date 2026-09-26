#!/usr/bin/env python3
"""Combine a Pico SDK firmware UF2 with a checked QPAK at flash offset 1 MiB.

No data is written to a device. The last 256 KiB is omitted (save partition).
"""
import argparse
from pathlib import Path
import struct
import zlib

MAGIC0, MAGIC1, END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
BASE, ASSET, SAVE = 0x10000000, 0x10100000, 0x10FC0000
GUARD = ASSET - 4096


def combine(firmware, assets):
    if not firmware or len(firmware) % 512:
        raise ValueError("Invalid firmware UF2 length")
    records = []
    family = None
    prefix = b""
    addresses = set()
    for pos in range(0, len(firmware), 512):
        block = firmware[pos:pos + 512]
        m0, m1, flags, address, size, index, count, fid = struct.unpack_from("<8I", block)
        if (m0, m1, struct.unpack_from("<I", block, 508)[0]) != (MAGIC0, MAGIC1, END):
            raise ValueError("Invalid UF2 magic")
        if flags == 0xA000 and fid == 0xE48BFF57:
            # Preserve Pico SDK's RP2350-E10 absolute-family ignore block,
            # moving its possible write away from the persistent save sector.
            if (prefix or pos != 0 or size != 256 or index != 0 or count != 2 or
                    block[32:288] != b"\xef" * 256 or
                    struct.unpack_from("<I", block, 288)[0] != 0x9957E304):
                raise ValueError("Unknown absolute-family UF2 extension")
            guard = bytearray(block)
            struct.pack_into("<I", guard, 12, GUARD)
            prefix = bytes(guard)
            continue
        if flags != 0x2000 or size != 256 or address % 256:
            raise ValueError("Expected Pico SDK family-ID UF2 with 256-byte payloads")
        if not BASE <= address < GUARD or address + size > GUARD:
            raise ValueError("Firmware overlaps asset partition or is not XIP firmware")
        if address in addresses or index != len(records) or count != len(firmware) // 512 - bool(prefix):
            raise ValueError("Duplicate address or inconsistent UF2 block numbering")
        addresses.add(address)
        if family is None:
            family = fid
        if family != fid:
            raise ValueError("Mixed UF2 families")
        records.append((address, block[32:32 + size]))
    if len(assets) < 64 or assets[:8] != b"QRP2350\0":
        raise ValueError("Invalid QPAK")
    version, block_size, total = struct.unpack_from("<3I", assets, 8)
    payload, metadata_crc = struct.unpack_from("<2I", assets, 36)
    if (version != 1 or block_size != 4096 or total != len(assets) or
            len(assets) > SAVE - ASSET or not 64 <= payload <= len(assets) or
            zlib.crc32(assets[64:payload]) != metadata_crc):
        raise ValueError("QPAK size/version/metadata/budget check failed")
    for pos in range(0, len(assets), 256):
        records.append((ASSET + pos, assets[pos:pos + 256].ljust(256, b"\xff")))
    if family != 0xE48BFF59:
        raise ValueError("Expected RP2350 Arm secure firmware family")
    out = bytearray(prefix)
    for i, (address, data) in enumerate(records):
        if address + len(data) > SAVE:
            raise ValueError("Would overwrite save partition")
        out.extend(struct.pack("<8I", MAGIC0, MAGIC1, 0x2000, address, 256, i, len(records), family))
        out.extend(data)
        out.extend(b"\0" * 220)
        out.extend(struct.pack("<I", END))
    return bytes(out)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("firmware", type=Path)
    p.add_argument("assets", type=Path)
    p.add_argument("output", type=Path)
    args = p.parse_args()
    result = combine(args.firmware.read_bytes(), args.assets.read_bytes())
    args.output.write_bytes(result)
    print(f"Wrote {args.output}: {len(result):,} UF2 bytes (container, not Flash usage)")


if __name__ == "__main__":
    main()
