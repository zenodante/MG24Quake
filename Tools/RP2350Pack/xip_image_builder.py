#!/usr/bin/env python3
"""Build deduplicated QXIP3 with immutable QLV1 brush models.

Use --level-format bsp29 for the legacy QXIP2 compatibility image.
The default QXIP3 format requires a new consumer; it is not a firmware update.
"""
from __future__ import annotations
import argparse, hashlib, json, struct
from collections import OrderedDict
from pathlib import Path
from mcu_pack_converter import read_pak, parse_bsp
from level_image import compile_level
from flash_layout import SAVE, ASSET
from resource_text import is_text_resource

MAGIC=b'QXIP'; VERSION=2; ALIGN=4
HEADER=struct.Struct('<4s11I')
DIR=struct.Struct('<IIII')
BSP_HEADER=struct.Struct('<I30I')
TEX_HDR=struct.Struct('<4sII')
TEX_ENT=struct.Struct('<II32s')
MAP_HDR=struct.Struct('<4sI')
MAP_ENT=struct.Struct('<IIII') # file dir index, local count, map byte offset, reserved
KIND_FILE=0; KIND_LEVEL=1
INVALID_TEX_ID=0xffffffff

def a4(n): return (n+3)&~3

def miptex_records(blob):
    if not blob:
        return [], []
    if len(blob) < 4:
        raise ValueError('truncated miptex directory')
    n = struct.unpack_from('<i', blob)[0]
    if n < 0 or 4+4*n > len(blob):
        raise ValueError('invalid miptex directory')
    rels = list(struct.unpack_from('<'+'i'*n, blob, 4))
    records = []
    intervals = []
    for rel in rels:
        if rel == -1:
            records.append(None)
            continue
        if rel < 4+4*n or rel+40 > len(blob):
            raise ValueError('miptex header outside lump')
        _, w, h, *offsets = struct.unpack_from('<16s6I', blob, rel)
        if not w or not h or w % 16 or h % 16:
            raise ValueError('invalid miptex dimensions')
        end = 40
        for mip, off in enumerate(offsets):
            size = (w >> mip)*(h >> mip)
            if off < end or rel+off+size > len(blob):
                raise ValueError('overlapping/truncated mip pixels')
            end = off+size
        records.append(blob[rel:rel+end])
        intervals.append((rel, rel+end))
    unique = sorted(set(intervals))
    if any(a[1] > b[0] for a,b in zip(unique, unique[1:])):
        raise ValueError('overlapping texture records')
    return rels, records

def build_level(data,tex_ids):
    lumps=parse_bsp(data);out=bytearray(BSP_HEADER.size);pairs=[];tex_dir_off=None
    for i,(_,_,blob) in enumerate(lumps):
        while len(out)%ALIGN:out.append(0)
        off=len(out)
        if i==2:
            tex_dir_off=off;out.extend(struct.pack('<i',len(tex_ids)));out.extend(b'\xff\xff\xff\xff'*len(tex_ids));size=4+4*len(tex_ids)
        else:out.extend(blob);size=len(blob)
        pairs.extend((off,size))
        if i == 0 and not blob.endswith(b"\0"): out.append(0)
    version=struct.unpack_from('<I',data,0)[0];BSP_HEADER.pack_into(out,0,version,*pairs);return out,tex_dir_off

def build(files, level_format="runtime", allow_oversize=False):
    if level_format not in ("runtime", "bsp29"):
        raise ValueError("unknown level format")
    runtime = level_format == "runtime"
    level_reports = {}
    level_records = {}
    texture_by_hash=OrderedDict();level_specs={};input_tex_bytes=0
    for name,data in files.items():
        if not name.lower().endswith('.bsp'):continue
        _,records=miptex_records(parse_bsp(data)[2][2]);ids=[]
        level_records[name] = records
        for rec in records:
            if rec is None:ids.append(None);continue
            input_tex_bytes+=len(rec);h=hashlib.sha256(rec).digest()
            if h not in texture_by_hash:texture_by_hash[h]={'id':len(texture_by_hash),'data':rec,'uses':0}
            texture_by_hash[h]['uses']+=1;ids.append(texture_by_hash[h]['id'])
        level_specs[name]=ids
    unique_tex_bytes=sum(len(x['data']) for x in texture_by_hash.values());payloads=[]
    for name,data in files.items():
        if name in level_specs:
            if runtime:
                try:
                    level, level_reports[name] = compile_level(data, level_specs[name], level_records[name])
                except (ValueError, OverflowError, struct.error) as exc:
                    raise ValueError(f'{name}: {exc}') from exc
                tdir = None
            else:
                level,tdir=build_level(data,level_specs[name])
            payloads.append([name,KIND_LEVEL,level,tdir])
        else:payloads.append([name,KIND_FILE,bytearray(data),None])
    strings=bytearray();name_off={}
    for name,_,_,_ in payloads:name_off[name]=len(strings);strings.extend(name.encode()+b'\0')
    str_off=HEADER.size;dir_off=a4(str_off+len(strings));data_off=a4(dir_off+DIR.size*len(payloads));pos=data_off;dirs=[];payload_off={}
    for name,kind,data,_ in payloads:
        pos=a4(pos);payload_off[name]=pos;dirs.append((name_off[name],kind,pos,len(data)));pos+=len(data)
        if kind==KIND_FILE and is_text_resource(name): pos+=1
    tex_off=a4(pos);count=len(texture_by_hash);tex_cursor=tex_off+TEX_HDR.size+TEX_ENT.size*count;tex_abs=[0]*count;tex_entries=[]
    for h,x in texture_by_hash.items():
        tex_cursor=a4(tex_cursor);tex_abs[x['id']]=tex_cursor;tex_entries.append((tex_cursor-tex_off,len(x['data']),h,x['data']));tex_cursor+=len(x['data'])
    # Compatibility patch for the current loader.  This can disappear once the
    # native RP2350 level binder uses the explicit LMAP section below.
    for name,kind,data,tdir in payloads:
        if kind!=KIND_LEVEL or runtime:continue
        base=payload_off[name]+tdir
        for i,tid in enumerate(level_specs[name]):
            rel=-1 if tid is None else tex_abs[tid]-base
            if not -0x80000000<=rel<=0x7fffffff:raise ValueError(f'texture relative offset out of int32 range in {name}')
            struct.pack_into('<i',data,tdir+4+i*4,rel)
    image=bytearray(data_off);image[str_off:str_off+len(strings)]=strings
    for (name,kind,data,_),(_,_,off,_) in zip(payloads,dirs):
        if len(image)<off:image.extend(b'\0'*(off-len(image)))
        image.extend(data)
        if kind==KIND_FILE and is_text_resource(name): image.append(0)
    if len(image)<tex_off:image.extend(b'\0'*(tex_off-len(image)))
    image.extend(TEX_HDR.pack(b'TEX1',count,TEX_HDR.size));ent_pos=len(image);image.extend(b'\0'*(TEX_ENT.size*count))
    for i,(roff,size,h,rec) in enumerate(tex_entries):
        absolute=tex_off+roff
        if len(image)<absolute:image.extend(b'\0'*(absolute-len(image)))
        image.extend(rec);TEX_ENT.pack_into(image,ent_pos+i*TEX_ENT.size,roff,size,h)
    tex_size=len(image)-tex_off
    # LMAP is deliberately separate from BSP payloads.  One MAP_ENT per level
    # points to a uint32 array indexed by the original BSP local miptex number.
    map_off=a4(len(image))
    if len(image)<map_off:image.extend(b'\0'*(map_off-len(image)))
    levels=[(i,p[0]) for i,p in enumerate(payloads) if p[1]==KIND_LEVEL]
    image.extend(MAP_HDR.pack(b'LMAP',len(levels)));map_ent_pos=len(image);image.extend(b'\0'*(MAP_ENT.size*len(levels)))
    map_manifest={}
    for mi,(file_index,name) in enumerate(levels):
        ids=level_specs[name];arr_off=a4(len(image))
        if len(image)<arr_off:image.extend(b'\0'*(arr_off-len(image)))
        for tid in ids:image.extend(struct.pack('<I',INVALID_TEX_ID if tid is None else tid))
        MAP_ENT.pack_into(image,map_ent_pos+mi*MAP_ENT.size,file_index,len(ids),arr_off-map_off,0)
        map_manifest[name]={'file_index':file_index,'local_texture_count':len(ids),'map_offset':arr_off-map_off,'level_offset':payload_off[name],'level_bytes':len(payloads[file_index][2])}
    map_size=len(image)-map_off
    for i,d in enumerate(dirs):DIR.pack_into(image,dir_off+i*DIR.size,*d)
    HEADER.pack_into(image,0,MAGIC,3 if runtime else VERSION,len(payloads),str_off,dir_off,data_off,tex_off,tex_size,count,map_off,map_size,len(image))
    manifest={'format':'QXIP3' if runtime else 'QXIP2','level_format':'QLV1+LMAP' if runtime else 'BSP29+external-miptex+LMAP','files':len(payloads),'levels':len(level_specs),'texture_records':sum(len(x) for x in level_specs.values()),'unique_textures':count,'input_texture_record_bytes':input_tex_bytes,'unique_texture_record_bytes':unique_tex_bytes,'texture_record_savings':input_tex_bytes-unique_tex_bytes,'image_bytes':len(image),'texture_store_offset':tex_off,'texture_store_bytes':tex_size,'level_map_offset':map_off,'level_map_bytes':map_size,'level_maps':map_manifest}
    manifest['source_file_sha256'] = {name:hashlib.sha256(data).hexdigest() for name,data in files.items()}
    manifest['image_sha256'] = hashlib.sha256(image).hexdigest()
    manifest['runtime_levels'] = level_reports
    manifest['nul_terminated_text_files'] = [n for n in files if is_text_resource(n)]
    manifest['text_length_policy'] = 'Directory lengths exclude the additional trailing NUL; QLV entities preserve source length.'
    manifest['asset_capacity'] = SAVE-ASSET
    manifest['asset_headroom'] = SAVE-ASSET-len(image)
    from verify_xip import validate_qxip
    validate_qxip(bytes(image), require_text_terminators=True, check_capacity=not allow_oversize)
    return bytes(image),manifest


def mapping_header(image, manifest):
    """Metadata only: large local maps live once in immutable QXIP."""
    import re
    lines = ['/* Generated with this QXIP image; never an array of host pointers. */',
             '#ifndef QXIP_GENERATED_MAPS_H', '#define QXIP_GENERATED_MAPS_H',
             '#include <stdint.h>',
             f'#define QXIP_IMAGE_SHA256 "{hashlib.sha256(image).hexdigest()}"',
             f'#define QXIP_IMAGE_BYTES {len(image)}u',
             '#define QXIP_INVALID_ID UINT32_C(0xffffffff)',
             '/* LMAP offsets below are image-relative. Each entry is a LE uint32 TEX1 ID. */']
    lines += ['#define QLV_HEADER_BYTES 16u', '#define QLV_SECTION_COUNT 15u', '#define QLV_SECTION_ENTRY_BYTES 16u', '/* Section entries: LE uint32 offset, bytes, count, stride. */']
    from mcu_pack_converter import LUMP_NAMES
    for i, name in enumerate(LUMP_NAMES):
        lines.append(f'#define QLV_SECTION_{name.upper()} {i}u')
    used = set()
    for name, entry in manifest['level_maps'].items():
        label = re.sub('[^A-Z0-9]', '_', name.upper())
        if label in used:
            raise ValueError('C identifier collision in level names')
        used.add(label)
        lines += [f'#define QLEVEL_{label}_FILE_ID {entry["file_index"]}u',
                  f'#define QLEVEL_{label}_OFFSET {entry["level_offset"]}u',
                  f'#define QLEVEL_{label}_BYTES {entry["level_bytes"]}u',
                  f'#define QLEVEL_{label}_TEXTURE_COUNT {entry["local_texture_count"]}u',
                  f'#define QLEVEL_{label}_TEXTURE_MAP_OFFSET {manifest["level_map_offset"]+entry["map_offset"]}u']
        off = manifest['level_map_offset']+entry['map_offset']
        for i in range(entry['local_texture_count']):
            gid = struct.unpack_from('<I', image, off+i*4)[0]
            lines.append(f'#define QLEVEL_{label}_LOCAL_TEX_{i} UINT32_C({gid})')
    lines += ['#endif', '']
    return '\n'.join(lines)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('pak',type=Path);ap.add_argument('-o','--output',required=True,type=Path);ap.add_argument('--json',type=Path);ap.add_argument('--allow-oversize-host',action='store_true',help='Mac validation only; report but do not reject capacity overflow');ap.add_argument('--header',type=Path);ap.add_argument('--level-format',choices=('runtime','bsp29'),default='runtime');args=ap.parse_args();files=read_pak(args.pak);image,manifest=build(files,args.level_format,allow_oversize=args.allow_oversize_host);args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(image)
    if args.header:
        args.header.parent.mkdir(parents=True,exist_ok=True);args.header.write_text(mapping_header(image,manifest))
    if args.json:args.json.parent.mkdir(parents=True,exist_ok=True);args.json.write_text(json.dumps(manifest,indent=2)+'\n')
    print('RP2350 global-texture XIP image');print('===============================');print(f'files:                   {manifest["files"]}');print(f'levels:                  {manifest["levels"]}');print(f'texture records:         {manifest["texture_records"]}');print(f'unique textures:         {manifest["unique_textures"]}');print(f'texture bytes before:    {manifest["input_texture_record_bytes"]:,}');print(f'texture bytes unique:    {manifest["unique_texture_record_bytes"]:,}');print(f'real texture saving:     {manifest["texture_record_savings"]:,}');print(f'level texture maps:      {manifest["level_map_bytes"]:,} bytes');print(f'QXIP image:              {manifest["image_bytes"]:,} bytes')
    if manifest['asset_headroom'] < 0:
        print(f'Asset budget exceeded by {-manifest["asset_headroom"]:,} bytes (host validation image)')
        if not args.allow_oversize_host: raise SystemExit('QXIP exceeds 15 MiB asset partition')
if __name__=='__main__':main()
