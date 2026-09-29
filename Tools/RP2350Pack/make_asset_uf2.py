#!/usr/bin/env python3
"""Generate a standalone RP2350 UF2 containing only the immutable QXIP assets.

The first QXIP byte is written at the asset partition start (normally the
768 KiB flash offset, XIP address 0x100c0000). Firmware is not touched, so this UF2 only needs to be reflashed when assets
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


def validate_qxip(assets):
    from verify_xip import validate_qxip as validate
    info = validate(assets)
    return info['files'], info['textures']


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
    p.add_argument("assets", type=Path, help="QXIP asset image")
    p.add_argument("output", type=Path, help="standalone asset UF2")
    args = p.parse_args()

    assets = args.assets.read_bytes()
    if assets[:4]==b'QRN1':
        from verify_arm_native import validate
        info=validate(assets);files=info['files'];textures='native'
    else:files, textures = validate_qxip(assets)
    result = encode_assets(assets)
    args.output.write_bytes(result)

    print(f"{assets[:4].decode()}: {len(assets):,} bytes, {files} files, {textures} global textures")
    print(f"Flash range: 0x{ASSET:08x} .. 0x{ASSET + len(assets):08x}")
    print(f"Resource boundary: 0x{SAVE:08x}; no dedicated save partition")
    print(f"Wrote {args.output}: {len(result):,} UF2 bytes (container, not Flash usage)")
    print("This UF2 contains assets only; it does not overwrite firmware.")


if __name__ == "__main__":
    main()
