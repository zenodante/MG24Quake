#!/usr/bin/env python3
"""One-command RP2350 shareware asset analysis/conversion pipeline."""
from __future__ import annotations
import argparse,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];RP=ROOT/'Tools'/'RP2350Pack'
def run(cmd):print('+',' '.join(map(str,cmd)),flush=True);subprocess.run([str(x) for x in cmd],check=True)
def analyze(pak,outpak,jsonfile,firmware,detail=False):
 cmd=[sys.executable,RP/'build_assets.py',pak,'-o',outpak,'--json',jsonfile,'--firmware-bytes',hex(firmware)];cmd+=['--detail'] if detail else [];print('+',' '.join(map(str,cmd)),flush=True);p=subprocess.run([str(x) for x in cmd]);
 if p.returncode not in(0,2):raise subprocess.CalledProcessError(p.returncode,cmd)
def mib(n):return n/1048576
def summary(original,converted,manifest,qxip_manifest,firmware,out):
 a=json.loads(Path(original).read_text());b=json.loads(Path(converted).read_text());m=json.loads(Path(manifest).read_text());q=json.loads(Path(qxip_manifest).read_text());mt=m.get('mdl_totals',{});s=m.get('sound_totals',{});budget=16*1048576-firmware
 rows={'original_xip_bytes':a['flash']['asset_image_bytes'],'python_output_xip_bytes':b['flash']['asset_image_bytes'],'python_converter_delta_bytes':b['flash']['asset_image_bytes']-a['flash']['asset_image_bytes'],'mdl_input_bytes':mt.get('input_bytes',0),'mdl_output_bytes':mt.get('output_bytes',0),'mdl_delta_bytes':mt.get('delta_bytes',0),'sound':s,'real_qxip_texture_saving_bytes':q['texture_record_savings'],'real_qxip_image_bytes':q['image_bytes'],'asset_budget_bytes':budget};rows['real_qxip_headroom_bytes']=budget-q['image_bytes'];Path(out).write_text(json.dumps(rows,indent=2)+'\n')
 print('\nRP2350 Python conversion comparison\n===================================');print(f"Original aligned PAK:        {mib(rows['original_xip_bytes']):7.2f} MiB");print(f"Python converted PAK:        {mib(rows['python_output_xip_bytes']):7.2f} MiB");print(f"MDL total:                   {mib(rows['mdl_input_bytes']):7.2f} -> {mib(rows['mdl_output_bytes']):.2f} MiB ({mib(rows['mdl_delta_bytes']):+.2f})");print(f"Real global texture saving:  {mib(rows['real_qxip_texture_saving_bytes']):7.2f} MiB");print(f"Real QXIP asset image:       {mib(rows['real_qxip_image_bytes']):7.2f} MiB");print(f"Asset budget:                {mib(rows['asset_budget_bytes']):7.2f} MiB");print(f"Real QXIP headroom:          {mib(rows['real_qxip_headroom_bytes']):+7.2f} MiB")
 if s:
  original=s.get('source_file_bytes',0);adpcm=s.get('converted_bytes',0);saving=original-adpcm
  print('\nSound inventory\n===============');print(f"WAV files:                   {s.get('files',0)}");print(f"Original WAV files:          {mib(original):.2f} MiB");print(f"QAD1 block IMA-ADPCM:        {mib(adpcm):.2f} MiB");print(f"Flash saving:                {mib(saving):.2f} MiB ({saving/original*100:.1f}%)" if original else 'Flash saving:                n/a');print(f"Total duration:              {s.get('duration_seconds',0):.1f} s");print(f"Decoded samples:             {s.get('samples',0):,}");print(f"ADPCM blocks:                {s.get('blocks',0):,}");print('Decoded sample rate:         11025 Hz');print('Decoded channels:            mono');print('Storage format:              QAD1 4-bit IMA ADPCM');print('Block size:                  256 decoded samples');print(f"Source sample rates:         {s.get('source_rates',{})}");print(f"Source bit depths:           {s.get('source_bits',{})}");print(f"Conversion errors:           {s.get('errors',0)}")
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('pak0',type=Path);ap.add_argument('-o','--out-dir',type=Path,default=ROOT/'build'/'rp2350-assets');ap.add_argument('--firmware-bytes',type=lambda x:int(x,0),default=0x100000);ap.add_argument('--detail',action='store_true');ap.add_argument('--bsp-mode',choices=('analyze','copy'),default='analyze');ap.add_argument('--mnode-size',type=int,default=24);ap.add_argument('--mleaf-size',type=int,default=24);args=ap.parse_args();pak=args.pak0.resolve()
 if not pak.is_file():raise SystemExit(f'pak0 not found: {pak}')
 out=args.out_dir.resolve();out.mkdir(parents=True,exist_ok=True)
 # Inventory original BSPs before conversion.  This report is the design input
 # for moving deterministic Mod_LoadBrushModel work to the host converter.
 bsp_runtime=out/'bsp-runtime-analysis.json';run([sys.executable,RP/'bsp_runtime_analyzer.py',pak,'--json',bsp_runtime])
 converted=out/'pak0conv-python.pak';manifest=out/'python-conversion.json';run([sys.executable,RP/'mcu_pack_converter.py',pak,converted,'--manifest',manifest,'--bsp-mode',args.bsp_mode,'--mnode-size',args.mnode_size,'--mleaf-size',args.mleaf_size]);orig_xip=out/'pak0-original-xip.pak';orig_json=out/'pak0-original.json';conv_xip=out/'pak0-python-xip.pak';conv_json=out/'pak0-python.json';analyze(pak,orig_xip,orig_json,args.firmware_bytes,args.detail);analyze(converted,conv_xip,conv_json,args.firmware_bytes,args.detail);qxip=out/'quake-assets.qxip';qxip_json=out/'quake-assets-qxip.json';run([sys.executable,RP/'xip_image_builder.py',converted,'-o',qxip,'--json',qxip_json]);summary(orig_json,conv_json,manifest,qxip_json,args.firmware_bytes,out/'summary.json');print('\nOutputs:');print(f'  BSP runtime analysis: {bsp_runtime}');print(f'  real QXIP image:      {qxip}');print(f'  comparison:           {out/"summary.json"}')
if __name__=='__main__':main()
