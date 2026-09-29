#!/usr/bin/env python3
"""Offline Mac native-ABI image generation, using the engine's C layout.

The output is host-only simulated Flash. It is NOT an RP2350 Flash image.
The C compiler emits layouts and relocations; this wrapper pairs artifacts,
checks their structure, and generates the fixed entry-offset header.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import zlib


def validate(data, source):
    if len(data) < 64:
        raise ValueError('truncated QNAT header')
    magic, version, size, count, source_size, source_crc, abi, pointer_size, crc = struct.unpack_from('<4s8I', data)
    if magic != b'QNAT' or version != 1 or size != len(data) or pointer_size != 8:
        raise ValueError('invalid QNAT1 header / host ABI')
    if not count or 64 + count * 64 > size:
        raise ValueError('invalid QNAT directory')
    if source_size != len(source) or source_crc != zlib.crc32(source) or crc != zlib.crc32(data[64:]):
        raise ValueError('QNAT/source checksum mismatch')
    levels = []
    used = set()
    end = 64 + count * 64
    for i in range(count):
        raw, base, length, rel, n = struct.unpack_from('<48s4I', data, 64+i*64)
        if b'\0' not in raw:
            raise ValueError('unterminated model name')
        name = raw.split(b'\0', 1)[0].decode('ascii')
        if name in used or base != (end+7)&~7 or base % 8 or rel != base+length or rel+n*8 > size:
            raise ValueError('invalid model/relocation ranges')
        used.add(name)
        slots = set()
        for j in range(n):
            slot, target = struct.unpack_from('<2I', data, rel+8*j)
            offset = target & 0x7fffffff
            if slot % 8 or not base <= slot <= base+length-8 or slot in slots or any(data[slot:slot+8]):
                raise ValueError('invalid/duplicate/nonzero pointer slot')
            if offset >= (source_size if target & 0x80000000 else size):
                raise ValueError('invalid relocation target')
            slots.add(slot)
        end = rel+n*8
        levels.append(dict(name=name, model_offset=base, immutable_bytes=length, relocation_count=n))
    if (end+7)&~7 != size:
        raise ValueError('unreferenced trailing bytes')
    return dict(format='QNAT1', target='mac-native-64', native_bytes=size, abi=abi,
                source_sha256=hashlib.sha256(source).hexdigest(), native_sha256=hashlib.sha256(data).hexdigest(),
                relocation_count=sum(x['relocation_count'] for x in levels), levels=levels)


def generate_header(manifest):
    rows=['/* Generated Mac-only entry offsets. Pair with the exact QXIP/QNAT artifacts. */',
          '#ifndef QNATIVE_ASSETS_H', '#define QNATIVE_ASSETS_H', '#include <stdint.h>',
          f'#define QNATIVE_ABI 0x{manifest["abi"]:08x}u',
          f'#define QNATIVE_SOURCE_SHA256 "{manifest["source_sha256"]}"',
          f'#define QNATIVE_IMAGE_SHA256 "{manifest["native_sha256"]}"']
    for level in manifest['levels']:
        symbol=re.sub('[^A-Za-z0-9]', '_', level['name']).upper()
        rows.append(f'#define QNATIVE_{symbol}_OFFSET {level["model_offset"]}u')
    return '\n'.join(rows+['#endif', ''])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('qxip',type=Path);p.add_argument('--packer',type=Path,required=True)
    p.add_argument('-o','--output',type=Path,required=True)
    p.add_argument('--header',type=Path);p.add_argument('--json',type=Path)
    a=p.parse_args()
    subprocess.run([str(a.packer.resolve()),str(a.qxip.resolve()),str(a.output.resolve())],check=True)
    manifest=validate(a.output.read_bytes(),a.qxip.read_bytes())
    if a.header:a.header.write_text(generate_header(manifest))
    if a.json:a.json.write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'Native immutable image: {manifest["native_bytes"]:,} bytes; runtime per-level metadata allocation: 0')

if __name__=='__main__':main()
