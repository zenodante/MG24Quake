#!/usr/bin/env python3
"""Lossless, seekable RP2350 asset image. Requires the host's liblz4.

All original entries (including demos and sound) are preserved byte for byte.
BSP geometry and PCM remain raw/XIP; only complete texture/lightmap blocks
may be compressed inside a BSP. No MG24 sky/triangle precomputation is added.
"""
import argparse
import ctypes
import ctypes.util
import hashlib
import json
from pathlib import Path
import struct
import zlib

MAGIC = b"QRP2350\0"
BLOCK = 4096
HEADER = struct.Struct("<8s14I")
ENTRY = struct.Struct("<56s6I")
PAGE = struct.Struct("<4I")
from flash_layout import ASSET, SAVE
ASSET_BUDGET = SAVE - ASSET


def read_pak(path):
    data = Path(path).read_bytes()
    if len(data) < 12:
        raise ValueError("Truncated PAK")
    magic, offset, length = struct.unpack_from("<4sII", data)
    if magic != b"PACK" or length % 64 or offset + length > len(data):
        raise ValueError("Invalid PAK directory")
    entries = []
    names = set()
    for pos in range(offset, offset + length, 64):
        raw, start, size = struct.unpack_from("<56sII", data, pos)
        name = raw.split(b"\0", 1)[0].decode("ascii")
        if not name or len(name) > 55 or name in names or start + size > len(data):
            raise ValueError("Invalid or duplicate entry: " + name)
        names.add(name)
        entries.append((name, data[start:start + size]))
    return data, entries


def compressor(path=None):
    candidates = [path] if path else [ctypes.util.find_library("lz4"),
                                    "/opt/homebrew/opt/lz4/lib/liblz4.dylib"]
    lib = None
    for candidate in candidates:
        if candidate:
            try:
                lib = ctypes.CDLL(candidate)
                break
            except OSError:
                pass
    if lib is None:
        raise RuntimeError("Install host liblz4 or pass --lz4-library PATH")
    lib.LZ4_compress_HC.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                  ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.LZ4_compress_HC.restype = ctypes.c_int

    def encode(data):
        out = ctypes.create_string_buffer(len(data) + len(data) // 255 + 16)
        size = lib.LZ4_compress_HC(data, out, len(data), len(out), 9)
        if size <= 0:
            raise RuntimeError("LZ4 compression failed")
        return out.raw[:size]
    return encode


def compressible_ranges(name, data):
    # Raw WAV permits the second core to mix without decoding or cache locks.
    if name.startswith("sound/") or name == "gfx/palette.lmp":
        return []
    if not name.endswith(".bsp"):
        return [(0, len(data))]
    if len(data) < 124 or struct.unpack_from("<I", data)[0] != 29:
        raise ValueError("Expected original Quake BSP29: " + name)
    ranges = []
    for lump in (2, 8):  # mip textures, lighting. Collision/geometry remain XIP.
        offset, size = struct.unpack_from("<II", data, 4 + lump * 8)
        if offset + size > len(data):
            raise ValueError("Invalid BSP lump: " + name)
        ranges.append((offset, offset + size))
    return ranges


def build(source, encode):
    original, files = read_pak(source)
    page_count = sum((len(data) + BLOCK - 1) // BLOCK for _, data in files)
    directory_offset = HEADER.size
    page_offset = directory_offset + ENTRY.size * len(files)
    payload_offset = page_offset + PAGE.size * page_count
    payload = bytearray()
    directory = bytearray()
    pages = bytearray()
    manifest = []
    page_index = 0
    for name, data in files:
        first = page_index
        ranges = compressible_ranges(name, data)
        stored_total = 0
        compressed_count = 0
        for start in range(0, len(data), BLOCK):
            block = data[start:start + BLOCK]
            packed = encode(block) if any(a <= start and start + len(block) <= b
                                         for a, b in ranges) else block
            compressed = len(packed) < len(block)
            if not compressed:
                packed = block
            pages.extend(PAGE.pack(payload_offset + len(payload), len(packed),
                                   len(block), int(compressed)))
            payload.extend(packed)
            payload.extend(b"\0" * (-len(payload) % 4))
            stored_total += len(packed)
            compressed_count += compressed
            page_index += 1
        crc = zlib.crc32(data)
        directory.extend(ENTRY.pack(name.encode(), len(data), first,
                                    page_index - first, crc, 0, 0))
        manifest.append(dict(name=name, original_bytes=len(data),
                             stored_bytes=stored_total, compressed_blocks=compressed_count,
                             crc32=f"{crc:08x}", sha256=hashlib.sha256(data).hexdigest()))
    metadata = directory + pages
    total = payload_offset + len(payload)
    header = HEADER.pack(MAGIC, 1, BLOCK, total, len(files), page_count,
                         directory_offset, page_offset, payload_offset,
                         zlib.crc32(metadata), 0, 0, 0, 0, 0)
    image = header + metadata + payload
    assert len(image) == total
    return image, dict(format="QRP2350 v1", block_bytes=BLOCK,
                       original_pak_sha256=hashlib.sha256(original).hexdigest(),
                       image_sha256=hashlib.sha256(image).hexdigest(),
                       image_bytes=total, budget_bytes=ASSET_BUDGET,
                       headroom_bytes=ASSET_BUDGET - total,
                       demos=[n for n, _ in files if n.endswith(".dem")],
                       music=[n for n, _ in files if n.startswith("music/")],
                       sound_entries=sum(n.startswith("sound/") for n, _ in files),
                       removed_entries=[], files=manifest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--lz4-library")
    args = parser.parse_args()
    image, report = build(args.input, compressor(args.lz4_library))
    if len(image) > ASSET_BUDGET:
        raise SystemExit(f"Resource budget exceeded: {len(image)} > {ASSET_BUDGET}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    args.output.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{len(image):,} bytes ({len(image)/2**20:.3f} MiB); "
          f"headroom {report['headroom_bytes']:,}; all {len(report['files'])} entries retained")


if __name__ == "__main__":
    main()
