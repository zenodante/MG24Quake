#!/usr/bin/env python3
"""One-command RP2350 shareware asset analysis/conversion pipeline.

Uses the portable Python MCUPackConverter implementation; no host C compiler or
host ABI emulation is required.
"""
from __future__ import annotations
import argparse, json, subprocess, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]; RP=ROOT/'Tools'/'RP2350Pack'

def run(cmd):
    print('+',' '.join(map(str,cmd)),flush=True); subprocess.run([str(x) for x in cmd],check=True)

def analyze(pak,outpak,jsonfile,firmware,detail=False):
    cmd=[sys.executable,RP/'build_assets.py',pak,'-o',outpak,'--json',jsonfile,'--firmware-bytes',hex(firmware)]
    if detail: cmd.append('--detail')
    print('+',' '.join(map(str,cmd)),flush=True); p=subprocess.run([str(x) for x in cmd])
    if p.returncode not in (0,2): raise subprocess.CalledProcessError(p.returncode,cmd)

def mib(n): return n/1048576

def summary(original,converted,manifest,out):
    a=json.loads(Path(original).read_text()); b=json.loads(Path(converted).read_text()); m=json.loads(Path(manifest).read_text())
    bsp=[x for x in m['files'] if 'projected_mg24_bytes' in x]
    raw_bsp=sum(x['input_bytes'] for x in bsp); projected_bsp=sum(x['projected_mg24_bytes'] for x in bsp)
    rows={'original_xip_bytes':a['flash']['asset_image_bytes'],'python_output_xip_bytes':b['flash']['asset_image_bytes'],
          'python_converter_delta_bytes':b['flash']['asset_image_bytes']-a['flash']['asset_image_bytes'],
          'projected_mg24_bsp_delta_bytes':projected_bsp-raw_bsp,
          'projected_mg24_bsp_bytes':projected_bsp,'raw_bsp_bytes':raw_bsp,
          'texture_duplicate_bytes':a['texture_dedup']['exact_duplicate_bytes'],'asset_budget_bytes':a['flash']['asset_budget_bytes']}
    # Projection: apply verified Python WAV changes, projected MG24 BSP node/leaf delta, then global texture dedup.
    rows['projected_asset_bytes']=rows['python_output_xip_bytes']+rows['projected_mg24_bsp_delta_bytes']-rows['texture_duplicate_bytes']
    rows['projected_headroom_bytes']=rows['asset_budget_bytes']-rows['projected_asset_bytes']
    Path(out).write_text(json.dumps(rows,indent=2)+'\n')
    print('\nRP2350 Python conversion comparison\n===================================')
    print(f"Original aligned PAK:        {mib(rows['original_xip_bytes']):7.2f} MiB")
    print(f"Python converted PAK:        {mib(rows['python_output_xip_bytes']):7.2f} MiB")
    print(f"Python direct delta:         {mib(rows['python_converter_delta_bytes']):+7.2f} MiB")
    print(f"MG24 BSP projected delta:    {mib(rows['projected_mg24_bsp_delta_bytes']):+7.2f} MiB")
    print(f"Global texture dedup:        {mib(rows['texture_duplicate_bytes']):7.2f} MiB")
    print(f"Projected asset image:       {mib(rows['projected_asset_bytes']):7.2f} MiB")
    print(f"Asset budget:                {mib(rows['asset_budget_bytes']):7.2f} MiB")
    print(f"Projected headroom:          {mib(rows['projected_headroom_bytes']):+7.2f} MiB")
    print('\nNOTE: MDL conversion is not included yet; BSP node/leaf size is a projection until target struct sizes are verified.')

def main():
    ap=argparse.ArgumentParser(description=__doc__); ap.add_argument('pak0',type=Path)
    ap.add_argument('-o','--out-dir',type=Path,default=ROOT/'build'/'rp2350-assets')
    ap.add_argument('--firmware-bytes',type=lambda x:int(x,0),default=0x180000); ap.add_argument('--detail',action='store_true')
    ap.add_argument('--bsp-mode',choices=('analyze','copy'),default='analyze')
    ap.add_argument('--mnode-size',type=int,default=24); ap.add_argument('--mleaf-size',type=int,default=24)
    args=ap.parse_args(); pak=args.pak0.resolve()
    if not pak.is_file(): raise SystemExit(f'pak0 not found: {pak}')
    out=args.out_dir.resolve(); out.mkdir(parents=True,exist_ok=True)
    converted=out/'pak0conv-python.pak'; manifest=out/'python-conversion.json'
    run([sys.executable,RP/'mcu_pack_converter.py',pak,converted,'--manifest',manifest,'--bsp-mode',args.bsp_mode,
         '--mnode-size',args.mnode_size,'--mleaf-size',args.mleaf_size])
    orig_xip=out/'pak0-original-xip.pak'; orig_json=out/'pak0-original.json'; conv_xip=out/'pak0-python-xip.pak'; conv_json=out/'pak0-python.json'
    analyze(pak,orig_xip,orig_json,args.firmware_bytes,args.detail); analyze(converted,conv_xip,conv_json,args.firmware_bytes,args.detail)
    summary(orig_json,conv_json,manifest,out/'summary.json')
    print('\nOutputs:'); print(f'  Python converted PAK: {converted}'); print(f'  conversion manifest: {manifest}'); print(f'  comparison:          {out/"summary.json"}')

if __name__=='__main__': main()
