#!/usr/bin/env python3
"""One-command RP2350 shareware asset preparation pipeline.

Usage from repository root:
  python3 Tools/RP2350Pack/run_pipeline.py /path/to/pak0.pak

The script:
  1. builds the original author's MCUPackConverter with the host C compiler;
  2. runs it on the shareware pak0.pak, preserving its proven MDL/BSP/sky/WAV
     preprocessing;
  3. runs build_assets.py on BOTH the original and converted PAK;
  4. writes JSON reports and a compact comparison summary.

This deliberately keeps the original PAK untouched.  It is also deliberately
separate from the later per-level internal-flash capture backend: this pipeline
provides the repeatable front end and measurements while that backend is wired
into the original model loader.
"""
from __future__ import annotations
import argparse, json, os, shutil, subprocess, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
MCU=ROOT/'Tools'/'MCUPackConverter'
RP=ROOT/'Tools'/'RP2350Pack'
SOURCES=['main.c','model.c','pakStringGenerator.c','sky.c','wavconverter.c']

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

def build_converter(out,cc):
    out.parent.mkdir(parents=True,exist_ok=True)
    src=[MCU/x for x in SOURCES]
    missing=[str(x) for x in src if not x.exists()]
    if missing: raise SystemExit('missing MCUPackConverter sources: '+', '.join(missing))
    cmd=[cc,'-std=c11','-O2','-Wall','-Wextra','-I',MCU,*src,'-lm','-o',out]
    run(cmd)

def analyze(pak,outpak,jsonfile,firmware,detail=False):
    cmd=[sys.executable,RP/'build_assets.py',pak,'-o',outpak,'--json',jsonfile,
         '--firmware-bytes',hex(firmware)]
    if detail: cmd.append('--detail')
    # build_assets exits 2 when an image does not fit. That is a measurement,
    # not a pipeline failure, so accept 0 or 2.
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
    run([exe,pak,converted],cwd=out)

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
