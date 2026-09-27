#!/usr/bin/env python3
"""Build the RP2350 runtime asset image with a real global BSP texture store.

This is deliberately a new RP2350 image format instead of pretending that a
standard Quake BSP can reference bytes outside its own texture lump.  Every BSP
is split into a compact level descriptor containing all non-texture lumps plus a
per-level array of global texture IDs.  Exact miptex records are stored once in
a global content-addressed texture store.  Same-name/different-content textures
remain distinct because identity is SHA-256 of the complete miptex record.

The current output is an offline/runtime-format proof: it makes the dedup saving
real in one binary image and emits a manifest.  Engine-side lookup can then map
(level texture index -> global texture ID -> XIP miptex bytes) without copying.
"""
from __future__ import annotations
import argparse, hashlib, json, struct
from collections import OrderedDict
from pathlib import Path
from mcu_pack_converter import read_pak, parse_bsp, LUMP_NAMES

MAGIC=b'QXIP'; VERSION=1; ALIGN=4
HEADER=struct.Struct('<4s9I')
DIR=struct.Struct('<IIII') # name_off, kind, data_off, data_size
LEVEL_HDR=struct.Struct('<4sI15I') # LVL1, texture_count, 15 section offsets; texture section is u32 IDs
TEX_HDR=struct.Struct('<4sII') # TEX1, count, directory offset
TEX_ENT=struct.Struct('<II32s') # record offset,size,sha256
KIND_FILE=0; KIND_LEVEL=1

def a4(n): return (n+3)&~3

def miptex_records(blob):
    if len(blob)<4: return [],[]
    n=struct.unpack_from('<i',blob,0)[0]
    if n<0 or 4+4*n>len(blob): raise ValueError('invalid miptex directory')
    rels=[struct.unpack_from('<i',blob,4+4*i)[0] for i in range(n)]
    valid=sorted({r for r in rels if r>=0})
    nxt={r:(valid[i+1] if i+1<len(valid) else len(blob)) for i,r in enumerate(valid)}
    records=[]
    for rel in rels:
        if rel<0: records.append(None); continue
        if rel+40>len(blob): raise ValueError('miptex header outside lump')
        raw,w,h,*_=struct.unpack_from('<16s6I',blob,rel)
        expected=40+(w*h*85)//64 if w and h else 40
        avail=max(0,min(nxt.get(rel,len(blob))-rel,len(blob)-rel))
        size=min(expected,avail) if avail else min(expected,len(blob)-rel)
        if size<40: raise ValueError('truncated miptex record')
        records.append(blob[rel:rel+size])
    return rels,records

def build_level(data,tex_ids):
    lumps=parse_bsp(data); out=bytearray(LEVEL_HDR.size); offsets=[0]*15
    for i,(_,_,blob) in enumerate(lumps):
        while len(out)%ALIGN: out.append(0)
        offsets[i]=len(out)
        if i==2:
            out.extend(struct.pack('<I',len(tex_ids)))
            for tid in tex_ids: out.extend(struct.pack('<I',0xffffffff if tid is None else tid))
        else: out.extend(blob)
    LEVEL_HDR.pack_into(out,0,b'LVL1',len(tex_ids),*offsets)
    return bytes(out)

def build(files):
    texture_by_hash=OrderedDict(); level_specs={}; input_tex_bytes=0
    for name,data in files.items():
        if not name.lower().endswith('.bsp'): continue
        lumps=parse_bsp(data); texblob=lumps[2][2]; _,records=miptex_records(texblob); ids=[]
        for rec in records:
            if rec is None: ids.append(None); continue
            input_tex_bytes+=len(rec); h=hashlib.sha256(rec).digest()
            if h not in texture_by_hash: texture_by_hash[h]={'id':len(texture_by_hash),'data':rec,'uses':0}
            texture_by_hash[h]['uses']+=1; ids.append(texture_by_hash[h]['id'])
        level_specs[name]=ids

    unique_tex_bytes=sum(len(x['data']) for x in texture_by_hash.values())
    payloads=[]
    for name,data in files.items():
        if name in level_specs: payloads.append((name,KIND_LEVEL,build_level(data,level_specs[name])))
        else: payloads.append((name,KIND_FILE,data))

    # String table first, then directory, payloads, then texture store.
    strings=bytearray(); name_off={}
    for name,_,_ in payloads:
        name_off[name]=len(strings); strings.extend(name.encode('utf-8')+b'\0')
    header_size=HEADER.size; str_off=header_size; dir_off=a4(str_off+len(strings)); data_off=a4(dir_off+DIR.size*len(payloads))
    image=bytearray(data_off); image[str_off:str_off+len(strings)]=strings; dirs=[]; pos=data_off
    for name,kind,data in payloads:
        pos=a4(pos)
        if len(image)<pos: image.extend(b'\0'*(pos-len(image)))
        off=pos; image.extend(data); pos+=len(data); dirs.append((name_off[name],kind,off,len(data)))
    tex_off=a4(len(image))
    if len(image)<tex_off: image.extend(b'\0'*(tex_off-len(image)))
    tex_start=tex_off; count=len(texture_by_hash); tex_dir_off=TEX_HDR.size; image.extend(TEX_HDR.pack(b'TEX1',count,tex_dir_off)); ent_pos=len(image); image.extend(b'\0'*(TEX_ENT.size*count))
    tex_entries=[]
    for h,x in texture_by_hash.items():
        while len(image)%ALIGN:image.append(0)
        roff=len(image)-tex_start; rec=x['data']; image.extend(rec); tex_entries.append((roff,len(rec),h))
    for i,e in enumerate(tex_entries): TEX_ENT.pack_into(image,ent_pos+i*TEX_ENT.size,*e)
    tex_size=len(image)-tex_start
    for i,d in enumerate(dirs): DIR.pack_into(image,dir_off+i*DIR.size,*d)
    HEADER.pack_into(image,0,MAGIC,VERSION,len(payloads),str_off,dir_off,data_off,tex_off,tex_size,count,len(image))
    return bytes(image),{'format':'QXIP1','files':len(payloads),'levels':len(level_specs),'texture_records':sum(len(x) for x in level_specs.values()),'unique_textures':count,'input_texture_record_bytes':input_tex_bytes,'unique_texture_record_bytes':unique_tex_bytes,'texture_record_savings':input_tex_bytes-unique_tex_bytes,'image_bytes':len(image),'texture_store_offset':tex_off,'texture_store_bytes':tex_size}

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('pak',type=Path); ap.add_argument('-o','--output',required=True,type=Path); ap.add_argument('--json',type=Path); args=ap.parse_args()
    files=read_pak(args.pak); image,manifest=build(files); args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_bytes(image)
    if args.json: args.json.parent.mkdir(parents=True,exist_ok=True); args.json.write_text(json.dumps(manifest,indent=2)+'\n')
    print('RP2350 global-texture XIP image'); print('==============================='); print(f'files:                   {manifest["files"]}'); print(f'levels:                  {manifest["levels"]}'); print(f'texture records:         {manifest["texture_records"]}'); print(f'unique textures:         {manifest["unique_textures"]}'); print(f'texture bytes before:    {manifest["input_texture_record_bytes"]:,}'); print(f'texture bytes unique:    {manifest["unique_texture_record_bytes"]:,}'); print(f'real texture saving:     {manifest["texture_record_savings"]:,}'); print(f'QXIP image:              {manifest["image_bytes"]:,} bytes')
if __name__=='__main__': main()
