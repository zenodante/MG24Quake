#!/usr/bin/env python3
"""Generate a standalone RP2350 UF2 containing only the immutable QXIP assets.

The first QXIP byte is written at the asset partition start (normally the
1 MiB flash offset, XIP address 0x10100000).  Firmware and the persistent save
partition are not touched, so this UF2 only needs to be reflashed when assets
change.
"""
import argparse
from pathlib import Path
import struct

from flash_layout import ASSET, SAVE

MAGIC0 = 0x0A324655
MAGIC1 = 0x9E5D5157
END = 0x0AB16F30
RP2350_ARM_SECURE_FAMILY = 0xE48BFF59
QXIP_HEADER = struct.Struct("<4s9I")
QXIP_MAGIC = b"QXIP"
QXIP_VERSION = 1


def validate_qxip(assets: bytes):
    if len(assets) < QXIP_HEADER.size:
        raise ValueError("QXIP image is smaller than its header")

    (magic, version, file_count, str_off, dir_off, data_off,
     tex_off, tex_size, texture_count, image_size) = QXIP_HEADER.unpack_from(assets, 0)

    if magic != QXIP_MAGIC or version != QXIP_VERSION:
        raise ValueError("Expected QXIP1 asset image")
    if image_size != len(assets):
        raise ValueError("QXIP header image size does not match file size")
    if len(assets) > SAVE - ASSET:
        raise ValueError("QXIP image does not fit before save partition")

    directory_bytes = file_count * 16
    if not (QXIP_HEADER.size <= str_off <= dir_off <= data_off <= tex_off <= len(assets)):
        raise ValueError("Invalid QXIP section ordering")
    if dir_off + directory_bytes > data_off:
        raise ValueError("QXIP directory overlaps payload area")
    if tex_off + tex_size != len(assets):
        raise ValueError("QXIP texture store size does not reach image end")
    if tex_size < 12 + texture_count * 40:
        raise ValueError("QXIP texture store is too small for its directory")
    if assets[tex_off:tex_off + 4] != b"TEX1":
        raise ValueError("Missing QXIP TEX1 texture store")

    return file_count, texture_count


def encode_assets(assets: bytes) -> bytes:
    records = []
    for pos in range(0, len(assets), 256):
        payload = assets[pos:pos + 256].ljust(256, b"\xff")
        address = ASSET + pos
        if address + len(payload) > SAVE:
            raise ValueError("Would overwrite save partition")
        records.append((address, payload))

    out = bytearray()
    count = len(records)
    for index, (address, payload) in enumerate(records):
        out.extend(struct.pack(
            "<8I", MAGIC0, MAGIC1, 0x2000, address, 256,
            index, count, RP2350_ARM_SECURE_FAMILY))
        out.extend(payload)
        out.extend(b"\0" * 220)
        out.extend(struct.pack("<I", END))
    return bytes(out)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("assets", type=Path, help="QXIP1 asset image")
    p.add_argument("output", type=Path, help="standalone asset UF2")
    args = p.parse_args()

    assets = args.assets.read_bytes()
    files, textures = validate_qxip(assets)
    result = encode_assets(assets)
    args.output.write_bytes(result)

    print(f"QXIP1: {len(assets):,} bytes, {files} files, {textures} global textures")
    print(f"Flash range: 0x{ASSET:08x} .. 0x{ASSET + len(assets):08x}")
    print(f"Save partition: 0x{SAVE:08x} (not written)")
    print(f"Wrote {args.output}: {len(result):,} UF2 bytes (container, not Flash usage)")
    print("This UF2 contains assets only; it does not overwrite firmware.")


if __name__ == "__main__":
    main()
