#!/usr/bin/env python3
"""Build the RP2350 XIP asset image with globally deduplicated BSP textures.

QXIP keeps each map directly consumable by the existing MG24 brush-model loader:
the level payload starts with a normal Quake BSP header and contains all ordinary
lumps unchanged.  Its texture lump contains only the dmiptex directory; each
signed dataofs points out of the level payload into the shared TEX1 texture
store.  RP2350 XIP makes those cross-payload relative pointers valid without
copying texture pixels or teaching the renderer a second model format.
"""
from __future__ import annotations
import argparse, hashlib, json, struct
from collections import OrderedDict
from pathlib import Path
from mcu_pack_converter import read_pak, parse_bsp

MAGIC=b'QXIP'; VERSION=1; ALIGN=4
HEADER=struct.Struct('<4s9I')
DIR=struct.Struct('<IIII')
BSP_HEADER=struct.Struct('<I30I')       # version + 15 (fileofs,filelen)
TEX_HDR=struct.Struct('<4sII')
TEX_ENT=struct.Struct('<II32s')
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
        _,w,h,*_=struct.unpack_from('<16s6I',blob,rel)
        expected=40+(w*h*85)//64 if w and h else 40
        avail=max(0,min(nxt.get(rel,len(blob))-rel,len(blob)-rel))
        size=min(expected,avail) if avail else min(expected,len(blob)-rel)
        if size<40: raise ValueError('truncated miptex record')
        records.append(blob[rel:rel+size])
    return rels,records

def build_level(data,tex_ids):
    """Return mutable BSP-compatible payload plus texture-directory offset.

    Texture dataofs entries are patched after the final QXIP/TEX1 layout is
    known.  The texture lump deliberately reports only its directory bytes;
    Mod_LoadTextures uses dataofs relative to that directory and can therefore
    reach the shared XIP records outside this level payload.
    """
    lumps=parse_bsp(data)
    out=bytearray(BSP_HEADER.size)
    pairs=[]; tex_dir_off=None
    for i,(_,_,blob) in enumerate(lumps):
        while len(out)%ALIGN: out.append(0)
        off=len(out)
        if i==2:
            tex_dir_off=off
            out.extend(struct.pack('<i',len(tex_ids)))
            out.extend(b'\xff\xff\xff\xff'*len(tex_ids))
            size=4+4*len(tex_ids)
        else:
            out.extend(blob); size=len(blob)
        pairs.extend((off,size))
    version=struct.unpack_from('<I',data,0)[0]
    BSP_HEADER.pack_into(out,0,version,*pairs)
    return out,tex_dir_off

def build(files):
    texture_by_hash=OrderedDict(); level_specs={}; input_tex_bytes=0
    for name,data in files.items():
        if not name.lower().endswith('.bsp'): continue
        texblob=parse_bsp(data)[2][2]; _,records=miptex_records(texblob); ids=[]
        for rec in records:
            if rec is None: ids.append(None); continue
            input_tex_bytes+=len(rec); h=hashlib.sha256(rec).digest()
            if h not in texture_by_hash:
                texture_by_hash[h]={'id':len(texture_by_hash),'data':rec,'uses':0}
            texture_by_hash[h]['uses']+=1; ids.append(texture_by_hash[h]['id'])
        level_specs[name]=ids

    unique_tex_bytes=sum(len(x['data']) for x in texture_by_hash.values())
    payloads=[]
    for name,data in files.items():
        if name in level_specs:
            level,tdir=build_level(data,level_specs[name])
            payloads.append([name,KIND_LEVEL,level,tdir])
        else:
            payloads.append([name,KIND_FILE,bytearray(data),None])

    strings=bytearray(); name_off={}
    for name,_,_,_ in payloads:
        name_off[name]=len(strings); strings.extend(name.encode('utf-8')+b'\0')
    str_off=HEADER.size; dir_off=a4(str_off+len(strings)); data_off=a4(dir_off+DIR.size*len(payloads))

    # Determine every payload's final image offset before writing TEX1.
    pos=data_off; dirs=[]; payload_off={}
    for name,kind,data,_ in payloads:
        pos=a4(pos); payload_off[name]=pos
        dirs.append((name_off[name],kind,pos,len(data))); pos+=len(data)
    tex_off=a4(pos); count=len(texture_by_hash); tex_dir_off=TEX_HDR.size

    # Determine final absolute position of every shared miptex record.
    tex_cursor=tex_off+TEX_HDR.size+TEX_ENT.size*count
    tex_abs=[0]*count; tex_entries=[]
    for h,x in texture_by_hash.items():
        tex_cursor=a4(tex_cursor); tex_abs[x['id']]=tex_cursor
        tex_entries.append((tex_cursor-tex_off,len(x['data']),h,x['data']))
        tex_cursor+=len(x['data'])

    # Patch dmiptex dataofs.  Offsets are signed and relative to the beginning
    # of the per-level texture directory, exactly as original Quake expects.
    for name,kind,data,tdir in payloads:
        if kind!=KIND_LEVEL: continue
        base=payload_off[name]+tdir
        for i,tid in enumerate(level_specs[name]):
            rel=-1 if tid is None else tex_abs[tid]-base
            if rel < -0x80000000 or rel > 0x7fffffff:
                raise ValueError(f'texture relative offset out of int32 range in {name}')
            struct.pack_into('<i',data,tdir+4+i*4,rel)

    image=bytearray(data_off); image[str_off:str_off+len(strings)]=strings
    for (name,kind,data,_),(_,_,off,_) in zip(payloads,dirs):
        if len(image)<off: image.extend(b'\0'*(off-len(image)))
        image.extend(data)
    if len(image)<tex_off: image.extend(b'\0'*(tex_off-len(image)))
    image.extend(TEX_HDR.pack(b'TEX1',count,tex_dir_off)); ent_pos=len(image)
    image.extend(b'\0'*(TEX_ENT.size*count))
    for i,(roff,size,h,rec) in enumerate(tex_entries):
        absolute=tex_off+roff
        if len(image)<absolute: image.extend(b'\0'*(absolute-len(image)))
        image.extend(rec); TEX_ENT.pack_into(image,ent_pos+i*TEX_ENT.size,roff,size,h)
    tex_size=len(image)-tex_off
    for i,d in enumerate(dirs): DIR.pack_into(image,dir_off+i*DIR.size,*d)
    HEADER.pack_into(image,0,MAGIC,VERSION,len(payloads),str_off,dir_off,data_off,tex_off,tex_size,count,len(image))
    return bytes(image),{'format':'QXIP1','level_format':'BSP29+external-miptex','files':len(payloads),'levels':len(level_specs),'texture_records':sum(len(x) for x in level_specs.values()),'unique_textures':count,'input_texture_record_bytes':input_tex_bytes,'unique_texture_record_bytes':unique_tex_bytes,'texture_record_savings':input_tex_bytes-unique_tex_bytes,'image_bytes':len(image),'texture_store_offset':tex_off,'texture_store_bytes':tex_size}

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('pak',type=Path); ap.add_argument('-o','--output',required=True,type=Path); ap.add_argument('--json',type=Path); args=ap.parse_args()
    files=read_pak(args.pak); image,manifest=build(files); args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_bytes(image)
    if args.json: args.json.parent.mkdir(parents=True,exist_ok=True); args.json.write_text(json.dumps(manifest,indent=2)+'\n')
    print('RP2350 global-texture XIP image'); print('==============================='); print(f'files:                   {manifest["files"]}'); print(f'levels:                  {manifest["levels"]}'); print(f'texture records:         {manifest["texture_records"]}'); print(f'unique textures:         {manifest["unique_textures"]}'); print(f'texture bytes before:    {manifest["input_texture_record_bytes"]:,}'); print(f'texture bytes unique:    {manifest["unique_texture_record_bytes"]:,}'); print(f'real texture saving:     {manifest["texture_record_savings"]:,}'); print(f'QXIP image:              {manifest["image_bytes"]:,} bytes')
if __name__=='__main__': main()
