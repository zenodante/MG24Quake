#!/usr/bin/env python3
"""Build the Mac player and offline native-image packer with clang + SDL2.
No generated IDE project is required. CMake remains supported separately.
"""
import argparse
import concurrent.futures
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MAC = ROOT / 'platform/macos'
SHARED = ROOT / 'platform/rp2350'
MG24 = ROOT / 'QuakeMG24/Quake'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('-o', '--output', type=Path, default=ROOT/'build-host/macos')
p.add_argument('--release', action='store_true')
p.add_argument('--sdl-config', default='/opt/homebrew/bin/sdl2-config')
a = p.parse_args()
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=True)
cflags = shlex.split(subprocess.check_output([a.sdl_config, '--cflags'], text=True))
libs = shlex.split(subprocess.check_output([a.sdl_config, '--libs'], text=True))
flags = ['-DQPAK_HOST_ALLOW_OVERSIZE=1','-std=gnu11','-O2','-g','-fno-omit-frame-pointer','-ffp-contract=off']
if not a.release:
    flags += ['-fsanitize=undefined','-fno-sanitize-recover=all']
include = [f'-I{x}' for x in (MAC, SHARED, MG24, ROOT/'QuakeMG24/src')]
engine = 'r_main r_misc r_bsp r_draw r_edge r_surf r_light r_efrag d_edge d_fill d_init d_modech d_scan d_sky d_vars mathlib tabmath cvar'.split()
sources = [(MG24/f'{s}.c', True) for s in engine]
sources += [(MAC/'mg24_renderer.c', True)]
sources += [(SHARED/f'{s}.c', False) for s in ('qpak','qbsp','qrender','qcollision','qmix')]
sources += [(MAC/f'{s}.c', False) for s in ('main','service_sdl','native_pack_main')]

def compile_one(pair):
    source, engine_source = pair
    obj = a.output/(source.stem+'.o')
    opts = ['-include', str(MAC/'mg24_config.h'), '-Wno-everything'] if engine_source else ['-Wall','-Wextra','-Werror','-DQR_XIP_ONLY=1','-DQMAC_MG24_RENDERER=1']
    subprocess.run(['clang', *flags, *include, *cflags, *opts, '-c', str(source), '-o', str(obj)], check=True)
    return source.stem, obj

with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
    objects = dict(pool.map(compile_one, sources))
for target, excluded in [('quake_mac', {'native_pack_main'}), ('qnative_pack', {'main','service_sdl'})]:
    subprocess.run(['clang', *flags, '-Wl,-dead_strip', f'-Wl,-map,{a.output/target}.map',
                    *(str(obj) for stem, obj in objects.items() if stem not in excluded),
                    *libs, '-o', str(a.output/target)], check=True)
print(f'Built {a.output}/quake_mac and qnative_pack')
