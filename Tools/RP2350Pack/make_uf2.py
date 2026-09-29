#!/usr/bin/env python3
"""Combine a Pico SDK RP2350 firmware UF2 with a checked QXIP asset image.

Firmware occupies the reserved firmware region.  The immutable QXIP image is
written at the configured asset partition start.  The persistent save partition
is never included in the generated UF2.
"""
import argparse
from pathlib import Path
import struct

from flash_layout import BASE, ASSET, SAVE, GUARD

MAGIC0, MAGIC1, END = 0x0A324655, 0x9E5D5157, 0x0AB16F30


def parse_firmware(firmware):
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
    if not records or family != 0xE48BFF59:
        raise ValueError("Expected RP2350 Arm secure firmware family")
    return prefix, records, family


def encode(prefix, records, family):
    out = bytearray(prefix)
    for i, (address, data) in enumerate(records):
        if address + len(data) > SAVE:
            raise ValueError("Would overwrite save partition")
        out.extend(struct.pack("<8I", MAGIC0, MAGIC1, 0x2000, address, 256, i, len(records), family))
        out.extend(data)
        out.extend(b"\0" * 220)
        out.extend(struct.pack("<I", END))
    return bytes(out)


def firmware_only(firmware):
    """Validate all writes and relocate E10 guard, without reading any assets."""
    return encode(*parse_firmware(firmware))


def validate_qxip(assets):
    from verify_xip import validate_qxip as validate
    info = validate(assets)
    return info


def combine(firmware, assets):
    prefix, records, family = parse_firmware(firmware)
    info = validate_qxip(assets)
    for pos in range(0, len(assets), 256):
        records.append((ASSET + pos, assets[pos:pos + 256].ljust(256, b"\xff")))
    return encode(prefix, records, family), info


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("firmware", type=Path,
                   help="Pico SDK RP2350 firmware UF2")
    p.add_argument("paths", type=Path, nargs="+")
    p.add_argument("--firmware-only", action="store_true",
                   help="firmware output: validate and relocate guard; assets are untouched")
    args = p.parse_args()

    if args.firmware_only:
        if len(args.paths) != 1:
            p.error("--firmware-only requires firmware and output paths")
        result = firmware_only(args.firmware.read_bytes())
        info = None
    else:
        if len(args.paths) != 2:
            p.error("requires firmware, QXIP assets and output paths")
        result, info = combine(args.firmware.read_bytes(), args.paths[0].read_bytes())

    output = args.paths[-1]
    output.write_bytes(result)
    print(f"Wrote {output}: {len(result):,} UF2 bytes (container, not Flash usage)")
    if info:
        print(f"QXIP: {info['bytes']:,} bytes, {info['files']} files, "
              f"{info['textures']} global textures @ flash 0x{ASSET:08x}")
        print(f"Resource boundary: 0x{SAVE:08x}; no dedicated save partition")


if __name__ == "__main__":
    main()
