#!/usr/bin/env python3
"""Prepare Quake resources for the RP2350 XIP-first port.

Merges pak0.pak and optional pak1.pak using normal Quake override semantics
(later PAK wins), and writes a conventional, uncompressed PAK whose file data
is 4-byte aligned.  The output deliberately performs no MG24 level-time flash
cache transformation: it is intended to be embedded once in the 16 MiB RP2350
XIP asset region and remain immutable at runtime.

This is a staging tool, not a renderer-specific converter.  Performance-derived
formats (alias triangle tables, sky preprocessing, etc.) should be added only
when the MG24 engine path proves that they are still beneficial on RP2350.
"""
import argparse
from collections import OrderedDict
from pathlib import Path
import struct

HEADER = struct.Struct("<4sII")
ENTRY = struct.Struct("<56sII")
ALIGN = 4


def read_pak(path):
    data = Path(path).read_bytes()
    if len(data) < HEADER.size:
        raise ValueError(f"{path}: truncated PAK")
    magic, directory, directory_bytes = HEADER.unpack_from(data)
    if magic != b"PACK" or directory_bytes % ENTRY.size:
        raise ValueError(f"{path}: invalid PAK header")
    if directory + directory_bytes > len(data):
        raise ValueError(f"{path}: directory outside file")
    out = []
    for pos in range(directory, directory + directory_bytes, ENTRY.size):
        raw_name, offset, size = ENTRY.unpack_from(data, pos)
        name = raw_name.split(b"\0", 1)[0].decode("ascii")
        if not name or offset + size > len(data):
            raise ValueError(f"{path}: invalid entry {name!r}")
        out.append((name, data[offset:offset + size]))
    return out


def merge(paths):
    files = OrderedDict()
    for path in paths:
        for name, data in read_pak(path):
            # Assignment keeps the original slot when overriding, which is fine:
            # Quake lookup semantics depend on the winning contents, not order.
            files[name] = data
    return files


def align(buf, n=ALIGN):
    buf.extend(b"\0" * (-len(buf) % n))


def build(files):
    image = bytearray(HEADER.size)
    records = []
    for name, data in files.items():
        align(image)
        offset = len(image)
        image.extend(data)
        records.append((name, offset, len(data)))
    align(image)
    directory = len(image)
    for name, offset, size in records:
        encoded = name.encode("ascii")
        if len(encoded) > 55:
            raise ValueError(f"PAK name too long: {name}")
        image.extend(ENTRY.pack(encoded.ljust(56, b"\0"), offset, size))
    HEADER.pack_into(image, 0, b"PACK", directory, len(records) * ENTRY.size)
    return bytes(image), records


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("inputs", nargs="+", type=Path,
                    help="PAKs in precedence order, normally pak0.pak pak1.pak")
    ap.add_argument("-o", "--output", required=True, type=Path)
    ap.add_argument("--budget", type=lambda s: int(s, 0), default=None,
                    help="optional maximum output bytes, e.g. 0x00E00000")
    args = ap.parse_args()
    files = merge(args.inputs)
    image, records = build(files)
    if args.budget is not None and len(image) > args.budget:
        raise SystemExit(f"resource budget exceeded: {len(image)} > {args.budget}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    print(f"wrote {args.output}: {len(image):,} bytes, {len(records)} files")
    for path in args.inputs:
        print(f"  source: {path}")


if __name__ == "__main__":
    main()
