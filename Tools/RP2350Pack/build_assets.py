#!/usr/bin/env python3
"""Build and analyze an XIP-first Quake asset image for RP2350.

This is deliberately a host-side tool.  It merges PAKs with normal Quake
precedence, inventories every asset, parses BSP lumps and MDL headers, emits an
uncompressed/aligned PAK suitable for immutable XIP, and produces JSON/text
reports for Flash and SRAM planning.

The report classifies data by *runtime semantics*, not by the MG24 storage
mechanism that happened to hold it:
  xip       immutable data expected to remain directly addressable in Flash
  sram      mutable/runtime-generated data that logically belongs in SRAM
  profile   immutable hot candidates: start in XIP, copy/cache only if measured

It does not yet rewrite BSP/MDL structures into a new ABI.  MG24 converter
transformations will be added here one at a time after their runtime consumers
are audited; the manifest is designed to make those decisions measurable.
"""
from __future__ import annotations
import argparse, json, struct
from collections import OrderedDict, defaultdict
from pathlib import Path

PAK_HEADER=struct.Struct('<4sII'); PAK_ENTRY=struct.Struct('<56sII')
BSP_HEADER=struct.Struct('<I'); BSP_LUMP=struct.Struct('<II')
BSP_VERSION=29; BSP_LUMPS=15; ALIGN=4
LUMP_NAMES=['entities','planes','textures','vertices','visibility','nodes',
            'texinfo','faces','lighting','clipnodes','leafs','marksurfaces',
            'edges','surfedges','models']
# Quake BSP29 on-disk record sizes. Variable/raw lumps are intentionally None.
LUMP_STRIDE={'planes':20,'vertices':12,'nodes':24,'texinfo':40,'faces':20,
             'clipnodes':8,'leafs':28,'marksurfaces':2,'edges':4,
             'surfedges':4,'models':64}


def align_up(n,a=ALIGN): return (n+a-1)&~(a-1)

def read_pak(path):
    blob=Path(path).read_bytes()
    if len(blob)<12: raise ValueError(f'{path}: truncated PAK')
    magic,doff,dlen=PAK_HEADER.unpack_from(blob)
    if magic!=b'PACK' or dlen%64 or doff+dlen>len(blob): raise ValueError(f'{path}: invalid PAK')
    out=[]
    for p in range(doff,doff+dlen,64):
        raw,off,size=PAK_ENTRY.unpack_from(blob,p); name=raw.split(b'\0',1)[0].decode('ascii')
        if not name or off+size>len(blob): raise ValueError(f'{path}: bad entry {name!r}')
        out.append((name,blob[off:off+size]))
    return out

def merge(paths):
    files=OrderedDict()
    for path in paths:
        for name,data in read_pak(path): files[name]=data
    return files

def classify_file(name):
    n=name.lower(); ext=Path(n).suffix
    if ext=='.bsp': return 'level','xip','BSP container; per-lump policy reported separately'
    if ext in ('.mdl','.spr'): return 'model','profile','immutable converted model data; profile renderer access'
    if ext in ('.wav',): return 'sound','xip','immutable sample/stream data'
    if ext in ('.lmp','.pcx'): return 'gfx','xip','immutable graphics/palette data'
    if n.endswith('progs.dat'): return 'gamecode','xip','immutable QuakeC program; mutable VM state is SRAM'
    if ext in ('.cfg','.rc','.txt'): return 'text','xip','cold immutable configuration/text'
    return 'other','xip','immutable resource by default'

def lump_policy(name):
    if name in ('nodes','planes','leafs','clipnodes','texinfo','faces','edges','surfedges','vertices','marksurfaces'):
        return 'profile','immutable traversal/render data; start XIP, promote only after profiling'
    if name in ('textures','lighting','visibility','entities'):
        return 'xip','immutable bulk/cold data; direct XIP baseline'
    if name=='models': return 'profile','immutable model descriptors; hotness must be measured'
    return 'xip','immutable'

def parse_bsp(name,data):
    if len(data)<4+8*BSP_LUMPS: return {'error':'truncated BSP'}
    version=struct.unpack_from('<I',data,0)[0]
    if version!=BSP_VERSION: return {'error':f'BSP version {version}, expected {BSP_VERSION}'}
    lumps=[]; off=4
    for lname in LUMP_NAMES:
        lo,ln=BSP_LUMP.unpack_from(data,off); off+=8
        if lo+ln>len(data): return {'error':f'{lname} outside BSP'}
        stride=LUMP_STRIDE.get(lname)
        lumps.append({'name':lname,'offset':lo,'bytes':ln,
                      'records':(ln//stride if stride and ln%stride==0 else None),
                      'stride':stride,'policy':lump_policy(lname)[0],
                      'reason':lump_policy(lname)[1]})
    return {'version':version,'bytes':len(data),'lumps':lumps}

def parse_mdl(data):
    if len(data)<84 or data[:4]!=b'IDPO': return None
    version=struct.unpack_from('<i',data,4)[0]
    skinw,skinh,numskins,numverts,numtris,numframes=struct.unpack_from('<6i',data,48)
    return {'version':version,'skinwidth':skinw,'skinheight':skinh,'skins':numskins,
            'vertices':numverts,'triangles':numtris,'frames':numframes,'bytes':len(data)}

def build_pak(files):
    image=bytearray(12); rec=[]
    for name,data in files.items():
        image.extend(b'\0'*(-len(image)%ALIGN)); pos=len(image); image.extend(data); rec.append((name,pos,len(data)))
    image.extend(b'\0'*(-len(image)%ALIGN)); directory=len(image)
    for name,pos,size in rec:
        enc=name.encode('ascii')
        if len(enc)>55: raise ValueError(f'PAK name too long: {name}')
        image.extend(PAK_ENTRY.pack(enc.ljust(56,b'\0'),pos,size))
    PAK_HEADER.pack_into(image,0,b'PACK',directory,len(rec)*64)
    return bytes(image),rec

def analyze(files,records,flash_bytes,firmware_bytes,sram_bytes):
    recmap={n:(o,s) for n,o,s in records}; assets=[]; levels=[]; cats=defaultdict(int); policies=defaultdict(int)
    for name,data in files.items():
        cat,pol,why=classify_file(name); cats[cat]+=len(data); policies[pol]+=len(data)
        a={'name':name,'bytes':len(data),'category':cat,'policy':pol,'reason':why,
           'xip_offset':recmap[name][0]}
        if name.lower().endswith('.bsp'):
            bsp=parse_bsp(name,data); a['bsp']=bsp
            if 'lumps' in bsp:
                lev={'name':name,'bytes':len(data),'lumps':bsp['lumps']}; levels.append(lev)
        elif name.lower().endswith('.mdl'):
            mdl=parse_mdl(data)
            if mdl: a['mdl']=mdl
        assets.append(a)
    asset_image_bytes=max((o+s for _,(o,s) in recmap.items()),default=12)
    # actual output includes aligned directory; caller patches exact size
    mandatory_sram=[
        {'name':'indexed framebuffer','bytes':320*200,'reason':'mutable per-frame render target'},
        {'name':'RGB565 line buffers','bytes':2*320*2,'reason':'Core-1 DMA ping-pong, one row each'},
    ]
    mandatory=sum(x['bytes'] for x in mandatory_sram)
    return {'flash':{'physical_bytes':flash_bytes,'firmware_reserved_bytes':firmware_bytes,
                     'asset_budget_bytes':max(0,flash_bytes-firmware_bytes)},
            'sram':{'physical_bytes':sram_bytes,'known_mandatory_bytes':mandatory,
                    'known_mandatory':mandatory_sram,
                    'unbudgeted_bytes':max(0,sram_bytes-mandatory),
                    'note':'renderer working sets, engine mutable state, stacks/audio and optional caches must be added from linker/runtime measurements'},
            'category_bytes':dict(sorted(cats.items())),'policy_bytes':dict(sorted(policies.items())),
            'levels':levels,'assets':assets}

def human(n):
    return f'{n:,} B ({n/1024/1024:.2f} MiB)'

def print_report(r):
    f=r['flash']; s=r['sram']
    print('RP2350 asset report')
    print('===================')
    print(f"XIP image:          {human(f['asset_image_bytes'])}")
    print(f"Firmware reserved:  {human(f['firmware_reserved_bytes'])}")
    print(f"Asset budget:       {human(f['asset_budget_bytes'])}")
    print(f"Flash remaining:    {human(f['asset_budget_bytes']-f['asset_image_bytes'])}")
    print(f"Flash status:       {'FIT' if f['fits'] else 'DOES NOT FIT'}")
    print('\nAsset policy bytes:')
    for k,v in r['policy_bytes'].items(): print(f'  {k:8s} {human(v)}')
    print('\nKnown mandatory SRAM (partial budget):')
    for x in s['known_mandatory']: print(f"  {x['name']:24s} {human(x['bytes'])}")
    print(f"  {'known subtotal':24s} {human(s['known_mandatory_bytes'])}")
    print(f"  {'remaining before engine':24s} {human(s['unbudgeted_bytes'])}")
    print('\nPer-level BSP storage:')
    for lev in r['levels']:
        p=defaultdict(int)
        for l in lev['lumps']: p[l['policy']]+=l['bytes']
        print(f"  {lev['name']}: {human(lev['bytes'])}  XIP={human(p['xip'])}  profile={human(p['profile'])}")

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('inputs',nargs='+',type=Path,help='PAKs in precedence order (pak0 then optional pak1)')
    ap.add_argument('-o','--output',required=True,type=Path,help='uncompressed aligned XIP PAK')
    ap.add_argument('--json',type=Path,help='write full machine-readable analysis')
    ap.add_argument('--flash-bytes',type=lambda x:int(x,0),default=16*1024*1024)
    ap.add_argument('--firmware-bytes',type=lambda x:int(x,0),default=0x180000,
                    help='flash reserved for firmware; default 1.5 MiB')
    ap.add_argument('--sram-bytes',type=lambda x:int(x,0),default=520*1024)
    args=ap.parse_args()
    files=merge(args.inputs); image,records=build_pak(files)
    report=analyze(files,records,args.flash_bytes,args.firmware_bytes,args.sram_bytes)
    report['flash']['asset_image_bytes']=len(image)
    report['flash']['fits']=len(image)<=report['flash']['asset_budget_bytes']
    args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_bytes(image)
    if args.json:
        args.json.parent.mkdir(parents=True,exist_ok=True); args.json.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print_report(report)
    if not report['flash']['fits']: raise SystemExit(2)

if __name__=='__main__': main()
