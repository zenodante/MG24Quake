#!/usr/bin/env python3
"""Build/analyze the RP2350 XIP-first Quake asset image.

Besides the raw aligned XIP PAK, this tool now measures the transformation
opportunities needed before defining the production runtime image:
  * complete BSP29 lump breakdown per level and globally;
  * embedded BSP miptex parsing;
  * exact texture-content deduplication across levels (SHA-256 of the complete
    miptex record, including all mip levels);
  * name reuse/conflict reporting, so equal names with different bytes are not
    incorrectly deduplicated;
  * a projected size if exact duplicate BSP textures were stored once globally.

No runtime compression is assumed.  The dedup number is an analysis projection,
not yet a rewritten BSP ABI; production offsets are introduced only after the
MG24 texture consumer is audited.
"""
from __future__ import annotations
import argparse, hashlib, json, struct
from collections import OrderedDict, defaultdict
from pathlib import Path

PAK_HEADER=struct.Struct('<4sII'); PAK_ENTRY=struct.Struct('<56sII')
BSP_LUMP=struct.Struct('<II'); BSP_VERSION=29; BSP_LUMPS=15; ALIGN=4
LUMP_NAMES=['entities','planes','textures','vertices','visibility','nodes','texinfo','faces','lighting','clipnodes','leafs','marksurfaces','edges','surfedges','models']
LUMP_STRIDE={'planes':20,'vertices':12,'nodes':24,'texinfo':40,'faces':20,'clipnodes':8,'leafs':28,'marksurfaces':2,'edges':4,'surfedges':4,'models':64}


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
    if ext=='.wav': return 'sound','xip','immutable sample/stream data'
    if ext in ('.lmp','.pcx'): return 'gfx','xip','immutable graphics/palette data'
    if n.endswith('progs.dat'): return 'gamecode','xip','immutable QuakeC program; mutable VM state is SRAM'
    if ext in ('.cfg','.rc','.txt'): return 'text','xip','cold immutable configuration/text'
    return 'other','xip','immutable resource by default'

def lump_policy(name):
    if name in ('nodes','planes','leafs','clipnodes','texinfo','faces','edges','surfedges','vertices','marksurfaces','models'):
        return 'profile','immutable traversal/render data; direct XIP first'
    return 'xip','immutable bulk/cold data; direct XIP baseline'

def parse_miptex_lump(data,lo,ln):
    result={'count':0,'directory_bytes':0,'payload_bytes':0,'textures':[],'missing':0,'error':None}
    if ln<4: return result
    end=lo+ln; count=struct.unpack_from('<i',data,lo)[0]
    if count<0 or 4+4*count>ln:
        result['error']='invalid miptex directory'; return result
    result['count']=count; result['directory_bytes']=4+4*count
    rels=struct.unpack_from('<'+'i'*count,data,lo+4) if count else ()
    valid=sorted({r for r in rels if r>=0})
    next_by_rel={r:(valid[i+1] if i+1<len(valid) else ln) for i,r in enumerate(valid)}
    for index,rel in enumerate(rels):
        if rel<0:
            result['missing']+=1; continue
        pos=lo+rel
        if pos+40>end:
            result['error']=f'texture {index} header outside lump'; continue
        rawname,width,height,o1,o2,o3,o4=struct.unpack_from('<16s6I',data,pos)
        name=rawname.split(b'\0',1)[0].decode('latin1')
        # BSP miptex record normally occupies header + 1 + 1/4 + 1/16 + 1/64 pixels.
        expected=40+(width*height*85)//64 if width and height else 40
        available=max(0,min(next_by_rel.get(rel,ln)-rel,ln-rel))
        record_len=min(expected,available) if available else min(expected,ln-rel)
        if record_len<40: continue
        record=data[pos:pos+record_len]
        digest=hashlib.sha256(record).hexdigest()
        result['payload_bytes']+=record_len
        result['textures'].append({'index':index,'name':name,'width':width,'height':height,
            'bytes':record_len,'sha256':digest,'relative_offset':rel,
            'mip_offsets':[o1,o2,o3,o4]})
    return result

def parse_bsp(data):
    if len(data)<4+8*BSP_LUMPS: return {'error':'truncated BSP'}
    version=struct.unpack_from('<I',data,0)[0]
    if version!=BSP_VERSION: return {'error':f'BSP version {version}, expected {BSP_VERSION}'}
    lumps=[]; off=4; texinfo=None
    for lname in LUMP_NAMES:
        lo,ln=BSP_LUMP.unpack_from(data,off); off+=8
        if lo+ln>len(data): return {'error':f'{lname} outside BSP'}
        stride=LUMP_STRIDE.get(lname); pol,reason=lump_policy(lname)
        item={'name':lname,'offset':lo,'bytes':ln,'records':(ln//stride if stride and ln%stride==0 else None),
              'stride':stride,'policy':pol,'reason':reason}
        if lname=='textures':
            texinfo=parse_miptex_lump(data,lo,ln); item['miptex']=texinfo
        lumps.append(item)
    return {'version':version,'bytes':len(data),'lumps':lumps,'textures':texinfo}

def parse_mdl(data):
    if len(data)<84 or data[:4]!=b'IDPO': return None
    version=struct.unpack_from('<i',data,4)[0]
    skinw,skinh,numskins,numverts,numtris,numframes=struct.unpack_from('<6i',data,48)
    return {'version':version,'skinwidth':skinw,'skinheight':skinh,'skins':numskins,'vertices':numverts,'triangles':numtris,'frames':numframes,'bytes':len(data)}

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

def texture_dedup(levels):
    by_hash={}; by_name=defaultdict(set); total_records=0; total_bytes=0
    for lev in levels:
        tex=lev.get('textures') or {}
        for t in tex.get('textures',[]):
            total_records+=1; total_bytes+=t['bytes']; by_name[t['name'].lower()].add(t['sha256'])
            e=by_hash.setdefault(t['sha256'],{'name':t['name'],'bytes':t['bytes'],'width':t['width'],'height':t['height'],'uses':[]})
            e['uses'].append({'level':lev['name'],'index':t['index'],'name':t['name']})
    unique_bytes=sum(x['bytes'] for x in by_hash.values())
    duplicate_bytes=total_bytes-unique_bytes
    conflicts=[{'name':n,'variants':len(hs),'hashes':sorted(hs)} for n,hs in by_name.items() if len(hs)>1]
    repeated=sorted((dict(v,sha256=h) for h,v in by_hash.items() if len(v['uses'])>1),key=lambda x:x['bytes']*(len(x['uses'])-1),reverse=True)
    return {'records':total_records,'unique_records':len(by_hash),'record_bytes':total_bytes,
            'unique_record_bytes':unique_bytes,'exact_duplicate_bytes':duplicate_bytes,
            'name_conflicts':conflicts,'repeated_textures':repeated}

def analyze(files,records,flash_bytes,firmware_bytes,sram_bytes):
    recmap={n:(o,s) for n,o,s in records}; assets=[]; levels=[]; cats=defaultdict(int); policies=defaultdict(int); lump_totals=defaultdict(int)
    for name,data in files.items():
        cat,pol,why=classify_file(name); cats[cat]+=len(data); policies[pol]+=len(data)
        a={'name':name,'bytes':len(data),'category':cat,'policy':pol,'reason':why,'xip_offset':recmap[name][0]}
        if name.lower().endswith('.bsp'):
            bsp=parse_bsp(data); a['bsp']=bsp
            if 'lumps' in bsp:
                for l in bsp['lumps']: lump_totals[l['name']]+=l['bytes']
                levels.append({'name':name,'bytes':len(data),'lumps':bsp['lumps'],'textures':bsp.get('textures')})
        elif name.lower().endswith('.mdl'):
            mdl=parse_mdl(data)
            if mdl: a['mdl']=mdl
        assets.append(a)
    dedup=texture_dedup(levels)
    mandatory=[{'name':'indexed framebuffer','bytes':320*200,'reason':'mutable per-frame render target'},
               {'name':'RGB565 line buffers','bytes':2*320*2,'reason':'Core-1 DMA ping-pong, one row each'}]
    mandatory_bytes=sum(x['bytes'] for x in mandatory)
    return {'flash':{'physical_bytes':flash_bytes,'firmware_reserved_bytes':firmware_bytes,'asset_budget_bytes':max(0,flash_bytes-firmware_bytes)},
            'sram':{'physical_bytes':sram_bytes,'known_mandatory_bytes':mandatory_bytes,'known_mandatory':mandatory,
                    'unbudgeted_bytes':max(0,sram_bytes-mandatory_bytes),'note':'engine/renderer/audio/stacks and optional hot caches still require measured budgets'},
            'category_bytes':dict(sorted(cats.items())),'policy_bytes':dict(sorted(policies.items())),
            'bsp_lump_bytes':{n:lump_totals.get(n,0) for n in LUMP_NAMES},'texture_dedup':dedup,'levels':levels,'assets':assets}

def human(n): return f'{n:,} B ({n/1024/1024:.2f} MiB)'

def print_report(r,detail=False):
    f=r['flash']; s=r['sram']; d=r['texture_dedup']
    print('RP2350 asset report\n===================')
    print(f"XIP image:          {human(f['asset_image_bytes'])}\nFirmware reserved:  {human(f['firmware_reserved_bytes'])}\nAsset budget:       {human(f['asset_budget_bytes'])}\nFlash remaining:    {human(f['asset_budget_bytes']-f['asset_image_bytes'])}\nFlash status:       {'FIT' if f['fits'] else 'DOES NOT FIT'}")
    print('\nGlobal BSP lump bytes:')
    for n in LUMP_NAMES: print(f'  {n:14s} {human(r["bsp_lump_bytes"][n])}')
    print('\nEmbedded BSP texture dedup projection:')
    print(f"  texture records:          {d['records']:,}")
    print(f"  unique exact records:     {d['unique_records']:,}")
    print(f"  texture record bytes:     {human(d['record_bytes'])}")
    print(f"  unique texture bytes:     {human(d['unique_record_bytes'])}")
    print(f"  exact duplicate savings:  {human(d['exact_duplicate_bytes'])}")
    projected=f['asset_image_bytes']-d['exact_duplicate_bytes']
    print(f"  projected image*:         {human(projected)}")
    print(f"  projected remaining*:     {human(f['asset_budget_bytes']-projected)}")
    print('  *analysis only: excludes directory/reference overhead and does not rewrite BSP yet')
    print(f"  same-name conflicts:      {len(d['name_conflicts'])}")
    print('\nKnown mandatory SRAM (partial budget):')
    for x in s['known_mandatory']: print(f"  {x['name']:24s} {human(x['bytes'])}")
    print(f"  {'known subtotal':24s} {human(s['known_mandatory_bytes'])}\n  {'remaining before engine':24s} {human(s['unbudgeted_bytes'])}")
    print('\nPer-level BSP storage:')
    for lev in r['levels']:
        ld={x['name']:x['bytes'] for x in lev['lumps']}; tx=lev.get('textures') or {}
        print(f"  {lev['name']}: {human(lev['bytes'])}  textures={human(ld.get('textures',0))}  lighting={human(ld.get('lighting',0))}  vis={human(ld.get('visibility',0))}")
        if detail:
            print('    '+'  '.join(f'{n}={ld.get(n,0):,}' for n in LUMP_NAMES))
    if d['repeated_textures']:
        print('\nTop exact duplicate textures:')
        for t in d['repeated_textures'][:20]:
            saved=t['bytes']*(len(t['uses'])-1)
            print(f"  {t['name'][:16]:16s} {t['width']}x{t['height']}  uses={len(t['uses']):2d}  save={human(saved)}")
    if d['name_conflicts']:
        print('\nTexture names with different contents (must NOT be name-deduped):')
        for c in d['name_conflicts'][:20]: print(f"  {c['name']}: {c['variants']} variants")

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('inputs',nargs='+',type=Path,help='PAKs in precedence order')
    ap.add_argument('-o','--output',required=True,type=Path,help='uncompressed aligned XIP PAK')
    ap.add_argument('--json',type=Path,help='write full machine-readable analysis')
    ap.add_argument('--detail',action='store_true',help='print all BSP lump sizes for every level')
    ap.add_argument('--flash-bytes',type=lambda x:int(x,0),default=16*1024*1024)
    ap.add_argument('--firmware-bytes',type=lambda x:int(x,0),default=0x180000)
    ap.add_argument('--sram-bytes',type=lambda x:int(x,0),default=520*1024)
    args=ap.parse_args()
    files=merge(args.inputs); image,records=build_pak(files)
    report=analyze(files,records,args.flash_bytes,args.firmware_bytes,args.sram_bytes)
    report['flash']['asset_image_bytes']=len(image); report['flash']['fits']=len(image)<=report['flash']['asset_budget_bytes']
    args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_bytes(image)
    if args.json:
        args.json.parent.mkdir(parents=True,exist_ok=True); args.json.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print_report(report,args.detail)
    if not report['flash']['fits']: raise SystemExit(2)

if __name__=='__main__': main()
