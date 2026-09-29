#!/usr/bin/env python3
"""Measure a proposed native-pointer resource ABI with ARM GCC; audit, not image output."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from flash_layout import ASSET, SAVE
from verify_xip import validate_qxip

ROOT = Path(__file__).resolve().parents[2]
TYPES = ('model_t', 'brush_model_data_t', 'mplane_t', 'mnode_t', 'mleaf_t',
         'msurface_t', 'mtexinfo_t', 'texture_t', 'dclipnode_t')
FLAGS = ['-std=gnu11', '-mcpu=cortex-m33', '-mthumb', '-mfloat-abi=hard',
         '-mfpu=fpv5-sp-d16', '-w']


def measure(pico8c, compiler=None):
    candidates = sorted((pico8c/'third_party/toolchains').glob('*/bin/arm-none-eabi-gcc'))
    if compiler is None:
        if len(candidates) != 1:
            raise ValueError('Specify --compiler: expected exactly one ARM GCC under pico8c')
        compiler = candidates[0]
    compiler = compiler.resolve()
    objcopy = compiler.with_name('arm-none-eabi-objcopy')
    expressions = ['sizeof(void*)', 'EDICTS_USE_SHORT_PTR', 'LINKS_USE_SHORT_PTR']
    for typ in TYPES:
        expressions += [f'sizeof({typ})', f'_Alignof({typ})']
    source = '#include "quakedef.h"\nconst unsigned int abi[] __attribute__((section(".resource_abi"),used)) = {' + ','.join(expressions) + '};\n'
    with tempfile.TemporaryDirectory(prefix='quake-resource-abi-') as tmp:
        tmp = Path(tmp)
        (tmp/'probe.c').write_text(source)
        subprocess.run([str(compiler), *FLAGS, '-include', str(ROOT/'platform/macos/mg24_config.h'),
                        '-I'+str(ROOT/'QuakeMG24/Quake'), '-I'+str(ROOT/'QuakeMG24/src'),
                        '-c', str(tmp/'probe.c'), '-o', str(tmp/'probe.o')], check=True)
        subprocess.run([str(objcopy), '-O', 'binary', '--only-section=.resource_abi',
                        str(tmp/'probe.o'), str(tmp/'probe.bin')], check=True)
        values = struct.unpack('<'+'I'*len(expressions), (tmp/'probe.bin').read_bytes())
    if values[:3] != (4, 0, 0):
        raise ValueError('Expected 32-bit pointers and disabled SRAM short pointers')
    sdk = pico8c/'third_party/pico-sdk/pico_sdk_version.cmake'
    return dict(profile='proposed MG24 native-pointer ARM resource ABI; portable C configuration',
                compiler=str(compiler), compiler_version=subprocess.check_output([str(compiler), '--version'], text=True).splitlines()[0],
                flags=FLAGS, sdk_version_source=sdk.read_text() if sdk.is_file() else None,
                pointer_bytes=values[0], edicts_short_ptr=values[1], links_short_ptr=values[2],
                config_sha256=hashlib.sha256((ROOT/'platform/macos/mg24_config.h').read_bytes()).hexdigest(),
                types={t:dict(size=values[3+2*i], alignment=values[4+2*i]) for i,t in enumerate(TYPES)})


def audit(data, abi):
    validate_qxip(data)
    files, strings, directory = struct.unpack_from('<3I', data, 8)
    sizes = {k:v['size'] for k,v in abi['types'].items()}
    replacements = {1:'mplane_t', 5:'mnode_t', 6:'mtexinfo_t', 7:'msurface_t', 10:'mleaf_t'}
    levels = []
    for i in range(files):
        name, kind, off, length = struct.unpack_from('<4I', data, directory+i*16)
        if kind != 1:
            continue
        if data[off:off+4] != b'QLV1':
            raise ValueError('Target ABI audit requires runtime QLV1 levels')
        name = data[strings+name:data.index(b'\0', strings+name)].decode('ascii')
        sections = [struct.unpack_from('<4I', data, off+16+j*16) for j in range(15)]
        rows = []
        for lump, typ in replacements.items():
            _, old, count, _ = sections[lump]
            rows.append(dict(lump=lump, type=typ, count=count, source_bytes=old, native_bytes=count*sizes[typ]))
        _, old, count, _ = sections[2]
        rows.append(dict(lump=2, type='texture_t + texture_t*', count=count, source_bytes=old,
                         native_bytes=count*(sizes['texture_t']+abi['pointer_bytes'])))
        # Retain the original dmodels section conservatively; add native descriptors
        # for every inline model, not just the world used by the renderer harness.
        descriptors = sections[14][2]*(sizes['model_t']+sizes['brush_model_data_t'])
        hull0 = sections[5][2]*sizes['dclipnode_t']
        delta = sum(r['native_bytes']-r['source_bytes'] for r in rows)+descriptors
        levels.append(dict(name=name, replacements=rows, submodels=sections[14][2],
                           native_model_descriptors_bytes=descriptors, hull0_bytes=hull0,
                           replacement_delta_bytes=delta))
    replacement = len(data)+sum(x['replacement_delta_bytes'] for x in levels)
    hull0 = sum(x['hull0_bytes'] for x in levels)
    return dict(status='projection_only_not_a_linked_or_bootable_image', source_sha256=hashlib.sha256(data).hexdigest(),
                source_bytes=len(data), asset_capacity_bytes=SAVE-ASSET, abi=abi, levels=levels,
                replacement_with_all_model_descriptors_bytes=replacement,
                additional_mg24_hull0_bytes=hull0,
                projected_with_mg24_hull0_bytes=replacement+hull0,
                projected_headroom_bytes=SAVE-ASSET-replacement-hull0,
                assumptions=['Replace six QLV1 sections; do not append a second native copy.',
                             'Retain all other QXIP data including source dmodels, LMAP and directories.',
                             'Include model_t and brush_model_data_t for every world and inline model.',
                             'Include Mod_MakeHull0 output once per BSP, shared by inline models.',
                             'Fixed-address links need no on-device relocation table.'],
                unresolved=['Final container layout, linker alignment and entry table are not built.',
                            'Visibility offset/index representation requires final loader design.',
                            'Runtime model registry fields require immutable/mutable split before game integration.',
                            'Alias/sprite runtime expansion and full gameplay memory are not covered.'])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('qxip', type=Path)
    ap.add_argument('--pico8c-root', type=Path, default=ROOT.parent/'pico8c')
    ap.add_argument('--compiler', type=Path)
    ap.add_argument('--json', type=Path, required=True)
    args = ap.parse_args()
    report = audit(args.qxip.read_bytes(), measure(args.pico8c_root, args.compiler))
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(report, indent=2)+'\n')
    print('ARM native resource ABI audit (projection only)')
    print('Replacement + all model descriptors:', report['replacement_with_all_model_descriptors_bytes'])
    print('Additional MG24 hull0:', report['additional_mg24_hull0_bytes'])
    print('Projected headroom:', report['projected_headroom_bytes'])

if __name__ == '__main__':
    main()
