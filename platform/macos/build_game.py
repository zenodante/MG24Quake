#!/usr/bin/env python3
"""Build the full MG24 engine integration (separate from the renderer harness)."""
import argparse
import concurrent.futures
from pathlib import Path
import subprocess
import shlex
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('-o',type=Path,default=ROOT/'build-host/macos-game')
p.add_argument('--compile-only',action='store_true')
p.add_argument('--memory-audit',action='store_true',help='Host-only allocation observer; excluded from target memory')
p.add_argument('--sanitize',action='store_true',help='Enable AddressSanitizer for host integration checks')
a=p.parse_args();a.o=a.o.resolve();a.o.mkdir(parents=True,exist_ok=True)
flags=['-DQPAK_HOST_ALLOW_OVERSIZE=1','-std=gnu11','-O1','-g','-fno-omit-frame-pointer','-ffp-contract=off','-Wno-everything','-Werror=implicit-function-declaration','-include',str(ROOT/'platform/macos/game_config.h')]
flags += ['-I'+str(ROOT/x) for x in ('platform/macos','platform/rp2350','QuakeMG24/Quake','QuakeMG24/src')]
flags += shlex.split(subprocess.check_output(['/opt/homebrew/bin/sdl2-config','--cflags'],text=True))
if a.memory_audit:flags += ['-DQMAC_MEMORY_AUDIT=1']
if a.sanitize:flags += ['-fsanitize=address']
exclude={'vid_sdl','snd_sdl','debug','snd_dma','snd_mem','snd_mix','d_surf','r_sky'}
sources=[s for s in (ROOT/'QuakeMG24/Quake').glob('*.c') if s.stem not in exclude]
sources += list((ROOT/'platform/macos').glob('game_*.c'))
sources += [ROOT/'platform/macos/service_sdl.c',ROOT/'platform/rp2350/qmix.c',ROOT/'platform/rp2350/qpak.c',ROOT/'platform/rp2350/qbsp.c']
def compile_one(s):
 obj=a.o/(s.stem+'.o')
 r=subprocess.run(['clang',*flags,'-c',str(s),'-o',str(obj)],capture_output=True,text=True)
 (a.o/(s.stem+'.log')).write_text(r.stderr)
 if r.returncode: raise RuntimeError(s.name+'\n'+r.stderr)
 return obj
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool: objects=list(pool.map(compile_one,sources))
if not a.compile_only:
 libs=shlex.split(subprocess.check_output(['/opt/homebrew/bin/sdl2-config','--libs'],text=True))
 subprocess.run(['clang',*(['-fsanitize=address'] if a.sanitize else []),'-Wl,-dead_strip',*map(str,objects),*libs,'-o',str(a.o/'quake_game')],check=True)
print('Compiled full engine:',len(objects),'modules')
