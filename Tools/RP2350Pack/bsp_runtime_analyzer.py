#!/usr/bin/env python3
"""Analyze BSP29 levels for RP2350 host-side runtime pre-expansion.

This tool does not change the asset format yet.  It inventories every BSP lump,
classifies data by intended RP2350 placement, and emits deterministic per-level
mapping metadata that later converter stages can serialize into QXIP.

The important distinction is between serialized BSP indices/offsets (which are
perfectly valid immutable resource references) and MG24's compressed runtime
pointer/storage representation (which RP2350 does not need to reproduce).
"""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from mcu_pack_converter import LUMP_NAMES, parse_bsp, read_pak
from xip_image_builder import miptex_records

# BSP29 on-disk element sizes.  Zero means variable-sized/raw data.
LUMP_ELEM_SIZE = {
    'planes': 20,
    'vertices': 12,
    'nodes': 24,
    'texinfo': 40,
    'faces': 20,
    'clipnodes': 8,
    'leafs': 28,
    'marksurfaces': 2,
    'edges': 4,
    'surfedges': 4,
    'models': 64,
}

# Initial architectural classification.  "split" means the source topology is
# immutable but the traditional Quake runtime struct also carries mutable frame
# state; a later pre-expansion stage should keep immutable data in XIP and only
# allocate a compact SRAM sidecar for mutable fields.
PLACEMENT = {
    'entities': ('xip_raw', 'immutable source text; parsed/consumed by game code'),
    'planes': ('xip_preexpand', 'immutable geometry'),
    'textures': ('xip_global_map', 'per-level local texture IDs map to global TEX1 IDs'),
    'vertices': ('xip_preexpand', 'immutable geometry'),
    'visibility': ('xip_raw', 'immutable PVS bitstream'),
    'nodes': ('xip_preexpand', 'immutable BSP topology; keep serialized child references or host-normalized IDs'),
    'texinfo': ('xip_preexpand', 'deterministic loader conversion: vectors, mipadjust, texture reference, flags'),
    'faces': ('split', 'immutable face/surface definition plus mutable renderer sidecar'),
    'lighting': ('xip_raw', 'immutable light samples'),
    'clipnodes': ('xip_preexpand', 'immutable collision topology'),
    'leafs': ('split', 'immutable BSP/PVS topology plus any mutable frame state in SRAM sidecar'),
    'marksurfaces': ('xip_preexpand', 'immutable surface references'),
    'edges': ('xip_preexpand', 'immutable geometry'),
    'surfedges': ('xip_preexpand', 'immutable geometry/index stream'),
    'models': ('xip_preexpand', 'immutable submodel definitions'),
}


def _count(name: str, size: int) -> int | None:
    elem = LUMP_ELEM_SIZE.get(name, 0)
    if not elem:
        return None
    if size % elem:
        raise ValueError(f'{name} lump size {size} is not divisible by {elem}')
    return size // elem


def analyze_level(name: str, data: bytes) -> dict:
    lumps = parse_bsp(data)
    result = {
        'name': name,
        'bsp_bytes': len(data),
        'lumps': [],
        'texture_local_count': 0,
        'texture_present_count': 0,
        'texture_record_bytes': 0,
        'static_source_bytes': 0,
        'split_source_bytes': 0,
    }
    for index, (off, size, blob) in enumerate(lumps):
        lname = LUMP_NAMES[index]
        placement, reason = PLACEMENT[lname]
        rec = {
            'index': index,
            'name': lname,
            'offset': off,
            'bytes': size,
            'count': _count(lname, size),
            'placement': placement,
            'reason': reason,
        }
        if lname == 'textures':
            rels, records = miptex_records(blob)
            rec['local_texture_count'] = len(rels)
            rec['present_texture_count'] = sum(x is not None for x in records)
            rec['record_bytes'] = sum(len(x) for x in records if x is not None)
            result['texture_local_count'] = rec['local_texture_count']
            result['texture_present_count'] = rec['present_texture_count']
            result['texture_record_bytes'] = rec['record_bytes']
        if placement == 'split':
            result['split_source_bytes'] += size
        else:
            result['static_source_bytes'] += size
        result['lumps'].append(rec)
    return result


def analyze_pak(path: Path) -> dict:
    files = read_pak(path)
    levels = []
    for name, data in files.items():
        if name.lower().endswith('.bsp'):
            levels.append(analyze_level(name, data))
    totals = {
        'levels': len(levels),
        'bsp_bytes': sum(x['bsp_bytes'] for x in levels),
        'texture_local_records': sum(x['texture_local_count'] for x in levels),
        'texture_present_records': sum(x['texture_present_count'] for x in levels),
        'texture_record_bytes': sum(x['texture_record_bytes'] for x in levels),
        'static_source_bytes': sum(x['static_source_bytes'] for x in levels),
        'split_source_bytes': sum(x['split_source_bytes'] for x in levels),
    }
    return {
        'format': 'RP2350-BSP-runtime-analysis-v1',
        'policy': {
            'immutable': 'pre-expand deterministic loader products on host and keep them in QXIP',
            'mutable': 'allocate only true runtime/frame/game state in SRAM',
            'references': 'use serialized IDs/offsets in QXIP; do not reproduce MG24 16-bit SRAM pointer compression',
            'split_structs': 'surface/leaf-style mixed structs should become immutable XIP data plus compact SRAM sidecars',
        },
        'totals': totals,
        'levels': levels,
    }


def print_summary(report: dict) -> None:
    t = report['totals']
    print('RP2350 BSP runtime-data analysis')
    print('================================')
    print(f"levels:                    {t['levels']}")
    print(f"BSP bytes:                 {t['bsp_bytes']:,}")
    print(f"local texture records:     {t['texture_local_records']:,}")
    print(f"present texture records:   {t['texture_present_records']:,}")
    print(f"texture record bytes:      {t['texture_record_bytes']:,}")
    print(f"static-source bytes:       {t['static_source_bytes']:,}")
    print(f"split-struct source bytes: {t['split_source_bytes']:,}")
    print('\nPer-level:')
    for level in report['levels']:
        counts = {x['name']: x['count'] for x in level['lumps'] if x['count'] is not None}
        print(
            f"  {level['name']:<20} {level['bsp_bytes']:>8,} B  "
            f"tex={level['texture_local_count']:>3} "
            f"nodes={counts.get('nodes', 0):>4} leafs={counts.get('leafs', 0):>4} "
            f"faces={counts.get('faces', 0):>5} texinfo={counts.get('texinfo', 0):>4}"
        )


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('pak', type=Path)
    ap.add_argument('--json', type=Path)
    args = ap.parse_args()
    report = analyze_pak(args.pak)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + '\n')
    print_summary(report)


if __name__ == '__main__':
    main()
