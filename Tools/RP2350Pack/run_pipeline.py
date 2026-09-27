#!/usr/bin/env python3
"""One-command RP2350 shareware asset preparation pipeline.

Usage from repository root:
  python3 Tools/RP2350Pack/run_pipeline.py /path/to/pak0.pak

The original MCUPackConverter was written around a 32-bit MCU ABI.  Building it
unchanged as a 64-bit macOS process changes pointer-bearing runtime structures
(texture_t, model-related structs, etc.), which can corrupt its generated brush
model representation and crash while converting pak0.  This wrapper therefore
builds a private host copy of the converter sources and injects
RP2350_HOST_CONVERTER.  The source tree itself remains the authoritative
conversion implementation; the host define only selects fixed-width serialized
fields where the converted representation stores MCU addresses/offsets.

The script:
  1. builds the author's MCUPackConverter as a host analysis tool;
  2. runs it on shareware pak0.pak;
  3. runs build_assets.py on original and converted PAKs;
  4. writes JSON reports and a compact comparison summary.
"""
from __future__ import annotations
import argparse, json, shutil, subprocess, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
MCU=ROOT/'Tools'/'MCUPackConverter'
RP=ROOT/'Tools'/'RP2350Pack'
SOURCES=['main.c','model.c','pakStringGenerator.c','sky.c','wavconverter.c']
HEADERS=['quakedef.h','model.h','modelgen.h','bspfile.h','mathlib.h','r_local.h','sky.h','wavconverter.h','pakStringGenerator.h']

def run(cmd,cwd=None):
    print('+',' '.join(map(str,cmd)),flush=True)
    subprocess.run([str(x) for x in cmd],cwd=cwd,check=True)

def compiler(name=None):
    if name:
        p=shutil.which(name)
        if not p: raise SystemExit(f'host C compiler not found: {name}')
        return p
    for c in ('cc','clang','gcc'):
        p=shutil.which(c)
        if p: return p
    raise SystemExit('no host C compiler found (tried cc, clang, gcc)')

def prepare_host_sources(dst):
    """Copy converter sources and patch only serialized pointer fields to u32.

    MG24/RP2350 are 32-bit targets.  On a 64-bit host, C pointers make texture_t
    larger and alter serialized layouts.  The converter does not need to
    dereference the final extmem texture addresses while producing the PAK, so
    representing those serialized addresses as uint32_t is the correct host ABI.
    """
    if dst.exists(): shutil.rmtree(dst)
    dst.mkdir(parents=True)
    for name in SOURCES+HEADERS:
        src=MCU/name
        if not src.exists(): raise SystemExit(f'missing MCUPackConverter source: {src}')
        shutil.copy2(src,dst/name)

    model=dst/'model.h'
    text=model.read_text()
    needle='\tuint8_t\t*extmemdata[MIPLEVELS];\t\t// four mip maps stored'
    repl='''#if RP2350_HOST_CONVERTER\n\tuint32_t\textmemdata[MIPLEVELS];\t// serialized 32-bit MCU addresses/offsets\n#else\n\tuint8_t\t*extmemdata[MIPLEVELS];\t\t// four mip maps stored\n#endif'''
    if needle not in text:
        raise SystemExit('host ABI patch failed: texture_t extmemdata declaration changed upstream')
    model.write_text(text.replace(needle,repl))

def build_converter(out,cc):
    out.parent.mkdir(parents=True,exist_ok=True)
    hostsrc=out.parent/'mcu-host-src'
    prepare_host_sources(hostsrc)
    src=[hostsrc/x for x in SOURCES]
    cmd=[cc,'-std=c11','-O2','-Wall','-Wextra','-DRP2350_HOST_CONVERTER=1','-I',hostsrc,*src,'-lm','-o',out]
    run(cmd)

def analyze(pak,outpak,jsonfile,firmware,detail=False):
    cmd=[sys.executable,RP/'build_assets.py',pak,'-o',outpak,'--json',jsonfile,
         '--firmware-bytes',hex(firmware)]
    if detail: cmd.append('--detail')
    print('+',' '.join(map(str,cmd)),flush=True)
    p=subprocess.run([str(x) for x in cmd])
    if p.returncode not in (0,2): raise subprocess.CalledProcessError(p.returncode,cmd)

def mib(n): return n/1024/1024

def summary(original,converted,out):
    a=json.loads(Path(original).read_text()); b=json.loads(Path(converted).read_text())
    rows={
      'original_xip_bytes':a['flash']['asset_image_bytes'],
      'mcu_converted_xip_bytes':b['flash']['asset_image_bytes'],
      'converter_delta_bytes':b['flash']['asset_image_bytes']-a['flash']['asset_image_bytes'],
      'original_texture_duplicate_bytes':a['texture_dedup']['exact_duplicate_bytes'],
      'converted_texture_duplicate_bytes':b['texture_dedup']['exact_duplicate_bytes'],
      'asset_budget_bytes':b['flash']['asset_budget_bytes'],
    }
    rows['converted_after_texture_dedup_projection_bytes']=rows['mcu_converted_xip_bytes']-rows['converted_texture_duplicate_bytes']
    rows['projected_headroom_bytes']=rows['asset_budget_bytes']-rows['converted_after_texture_dedup_projection_bytes']
    Path(out).write_text(json.dumps(rows,indent=2)+'\n')
    print('\nRP2350 conversion comparison\n============================')
    print(f"Original aligned PAK:       {mib(rows['original_xip_bytes']):7.2f} MiB")
    print(f"MG24 converted aligned PAK: {mib(rows['mcu_converted_xip_bytes']):7.2f} MiB")
    print(f"Converter delta:            {mib(rows['converter_delta_bytes']):+7.2f} MiB")
    print(f"Converted texture dedup:    {mib(rows['converted_texture_duplicate_bytes']):7.2f} MiB")
    print(f"Projected after dedup:      {mib(rows['converted_after_texture_dedup_projection_bytes']):7.2f} MiB")
    print(f"Asset budget:               {mib(rows['asset_budget_bytes']):7.2f} MiB")
    print(f"Projected headroom:         {mib(rows['projected_headroom_bytes']):+7.2f} MiB")
    print(f"Summary JSON: {out}")

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('pak0',type=Path,help='Quake shareware/freeware pak0.pak')
    ap.add_argument('-o','--out-dir',type=Path,default=ROOT/'build'/'rp2350-assets')
    ap.add_argument('--cc',help='host C compiler (default: cc/clang/gcc)')
    ap.add_argument('--firmware-bytes',type=lambda x:int(x,0),default=0x180000)
    ap.add_argument('--detail',action='store_true')
    ap.add_argument('--skip-build',action='store_true',help='reuse previously built MCUPackConverter')
    args=ap.parse_args()
    pak=args.pak0.resolve()
    if not pak.is_file(): raise SystemExit(f'pak0 not found: {pak}')
    out=args.out_dir.resolve(); out.mkdir(parents=True,exist_ok=True)
    exe=out/'MCUPackConverter'
    if not args.skip_build: build_converter(exe,compiler(args.cc))
    elif not exe.exists(): raise SystemExit(f'--skip-build requested but missing {exe}')

    converted=out/'pak0conv.pak'
    try:
        run([exe,pak,converted],cwd=out)
    except subprocess.CalledProcessError as e:
        if e.returncode in (-11,139):
            raise SystemExit('MCUPackConverter crashed with SIGSEGV. Re-run without --skip-build so the fixed 32-bit host serialization ABI is rebuilt.') from e
        raise

    orig_xip=out/'pak0-original-xip.pak'; orig_json=out/'pak0-original.json'
    conv_xip=out/'pak0-mg24-xip.pak'; conv_json=out/'pak0-mg24.json'
    analyze(pak,orig_xip,orig_json,args.firmware_bytes,args.detail)
    analyze(converted,conv_xip,conv_json,args.firmware_bytes,args.detail)
    summary(orig_json,conv_json,out/'summary.json')
    print('\nOutputs:')
    print(f'  MG24 converted PAK: {converted}')
    print(f'  original report:    {orig_json}')
    print(f'  converted report:   {conv_json}')
    print(f'  comparison:         {out/"summary.json"}')

if __name__=='__main__': main()
