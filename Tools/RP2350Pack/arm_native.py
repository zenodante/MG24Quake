#!/usr/bin/env python3
"""Compile QXIP3 to fixed-address ARM native resources, using engine C layouts.

No relocation table or brush allocations are needed on the device. The ARM
linker resolves all resource references independently of the firmware link.
"""
import argparse,json,math,struct,subprocess,zlib,hashlib
from arm_models import emit_alias,emit_sprite
from pathlib import Path
from flash_layout import ASSET,BASE,SAVE
ASSET_OFFSET=ASSET-BASE;XIP_BASE=BASE;SAVE_OFFSET=SAVE-BASE
ROOT=Path(__file__).resolve().parents[2]

def build(source,out,toolchain):
    q=source.read_bytes();u=lambda off:struct.unpack_from('<I',q,off)[0]
    if q[:4]!=b'QXIP' or u(4)!=3: raise ValueError('QXIP3 required')
    work=out.parent/(out.stem+'-link');work.mkdir(parents=True,exist_ok=True)
    c=['#include "quakedef.h"','#include "resource_format.h"','extern const unsigned char raw[];']
    raw=bytearray();entries=[];stats=[]
    def blob(data):
        raw.extend(b'\0'*((-len(raw))%4));off=len(raw);raw.extend(data);return off
    def ptr(off,typ='byte'):return f'({typ}*)(raw+{off})'
    def arr(typ,name,values):
        c.append(f'const {typ} {name}[{max(1,len(values))}] = {{'+',\n'.join(values or ['{0}'])+'};')
    def nums(v):return '{'+','.join(str(x) for x in v)+'}'
    def fl(v):return '{'+','.join(float(x).hex()+'f' for x in v)+'}'
    def ref(name,index,typ):return 'NULL' if index==0xffffffff else f'({typ}*)&{name}[{index}]'
    tex=[]
    for i in range(u(32)):
        off,size=struct.unpack_from('<II',q,u(24)+12+i*40);b=q[u(24)+off:u(24)+off+size]
        w,h,*mips=struct.unpack_from('<6I',b,16)
        dest=blob(b[40:]);tex.append((w,h,[dest+m-40 for m in mips]))
    for fi in range(u(8)):
        no,kind,off,size=struct.unpack_from('<4I',q,u(16)+16*fi)
        name=q[u(12)+no:q.index(0,u(12)+no)].decode('ascii');data=q[off:off+size]
        if not kind:
            if data[:4]==b'WAD2':
                normalized=bytearray(data);count_,table=struct.unpack_from('<II',data,4)
                if table+count_*32>len(data):raise ValueError('WAD directory overflow')
                for j in range(count_):
                    start=table+j*32+16;clean=data[start:start+16].split(b'\0')[0].lower()
                    normalized[start:start+16]=clean.ljust(16,b'\0')
                data=bytes(normalized)
            dest=blob(data+b'\0')
            if data[:4]==b'IDPX':entry=emit_alias(c,f'a{fi}',data,dest)
            elif data[:4]==b'IDSP':entry=emit_sprite(c,f's{fi}',data,dest)
            else:entry=(ptr(dest),str(size),0)
            entries.append((name,*entry));continue
        if data[:4]!=b'QLV1':raise ValueError(name)
        sec=[struct.unpack_from('<4I',data,16+i*16) for i in range(15)]
        def records(i,fmt):
            o,n,count,stride=sec[i];assert struct.calcsize(fmt)==stride
            return [struct.unpack_from(fmt,data,o+j*stride) for j in range(count)]
        prefix=f'l{fi}_';n=lambda s:prefix+s
        rawptr={}
        for i,typ in [(0,'char'),(3,'mvertex_t'),(4,'byte'),(8,'byte'),(11,'short'),(12,'medge_t'),(13,'short')]:
            o,s,_,_=sec[i];rawptr[i]=ptr(blob(data[o:o+s]+(b'\0' if i==0 else b'')),typ) if s else 'NULL'
        planes=records(1,'<4fBBH');nodes=records(5,'<I2h6h2Hi');leaves=records(10,'<ii6h2H4Bi')
        models=records(14,'<9f7i');surfaces=records(7,'<I4H4h4Bii')
        if len(nodes)>4096 or len(leaves)>8192 or len(models)>255:raise ValueError('BSP scalar capacity')
        arr('mplane_t',n('planes'),['{.normal='+fl(p[:3])+f',.fixed_dist={int(p[3]*256)},.type={p[4]},.signbits={p[5]}'+'}' for p in planes])
        textures=records(2,'<6I');c.append(f'extern const texture_t {n("textures")}[{max(1,len(textures))}];')
        values=[]
        for id,next_,alt,total,low,high in textures:
            w,h,m=(0,0,[None]*4) if id==0xffffffff else tex[id]
            if w>1023 or h>1023:raise ValueError('texture dimensions')
            values.append('{'+f'.width={w},.height={h},.anim_total={total},.anim_min={low},.anim_max={high},.anim_next={ref(n("textures"),next_,"texture_t")},.alternate_anims={ref(n("textures"),alt,"texture_t")},.extmemdata={{'+','.join('NULL' if x is None else ptr(x) for x in m)+'}}')
        arr('texture_t',n('textures'),values)
        arr('texture_t *const',n('textureptrs'),[ref(n('textures'),i,'texture_t') for i in range(len(textures))])
        arr('mtexinfo_t',n('texinfo'),['{.vecs={'+fl(p[:4])+','+fl(p[4:8])+'},'+f'.texture={ref(n("textures"),p[8],"texture_t")},.reduced_flags={p[9]&1},.mipadjust={p[10]}'+'}' for p in records(6,'<8f3I')])
        values=[]
        for i,p in enumerate(surfaces):
            if p[0]>32767 or p[3]>127:raise ValueError('surface scalar capacity')
            sample='NULL' if p[13]<0 else f'({rawptr[8]}+{p[13]})'
            values.append('{'+f'.firstedge={p[0]},.plane={ref(n("planes"),p[1],"mplane_t")},.texinfo={ref(n("texinfo"),p[2],"mtexinfo_t")},.numedges={p[3]},.flags={p[4]},.surfIdx={i},.surfNodeIndex={p[14]},.texturemins={nums(p[5:7])},.extents={nums(p[7:9])},.styles={nums(p[9:13])},.samples={sample}'+'}')
        arr('msurface_t',n('surfaces'),values)
        arr('mnode_t',n('nodes'),['{'+f'.node_idx={i},.parent_idx={p[11]},.plane={ref(n("planes"),p[0],"mplane_t")},.children_idx={nums(p[1:3])},.minmaxs={nums(p[3:9])},.firstsurface={p[9]},.numsurfaces={p[10]}'+'}' for i,p in enumerate(nodes)])
        values=[]
        for i,p in enumerate(leaves):
            if p[1]>=65535:raise ValueError('PVS offset exceeds native field')
            values.append('{'+f'.contents={p[0]},.parent_idx={p[14]},.leaf_idx={i},.compressed_vis_idx={p[1] if p[1]>=0 else 65535},.minmaxs={nums(p[2:8])},.firstMarkSurfaceIdx={p[8]},.nummarksurfaces={p[9]},.ambient_sound_level={nums(p[10:14])}'+'}')
        arr('mleaf_t',n('leafs'),values)
        arr('dclipnode_t',n('hull0'),['{'+f'.planenum={p[0]},.children={nums([leaves[-1-x][0] if x<0 else x for x in p[1:3]])}'+'}' for p in nodes])
        clips=records(9,'<i2h')
        arr('dclipnode_t',n('clip'),['{'+f'.planenum={p[0]},.children={nums(p[1:])}'+'}' for p in clips])
        brushes=[];descs=[]
        for i,p in enumerate(models):
            fields=[f'.numsubmodels={len(models)}',f'.numleafs={p[13]}',f'.numnodes={len(nodes)}',f'.numsurfaces={len(surfaces)}',f'.firstmodelsurface={p[14]}',f'.nummodelsurfaces={p[15]}']
            for field,typ in [('planes','mplane_t'),('leafs','mleaf_t'),('nodes','mnode_t'),('texinfo','mtexinfo_t'),('surfaces','msurface_t'),('textureptrs','texture_t*')]:
                fields.append(f'.{"textures" if field=="textureptrs" else field}=({typ}*){n(field)}')
            for field,j in [('entities',0),('vertexes',3),('visdata',4),('lightdata',8),('marksurfaceIdx',11),('edges',12),('surfedges',13)]:fields.append(f'.{field}={rawptr[j]}')
            hulls=[]
            for h in range(4):
                mins=[-16,-16,-24] if h==1 else [-32,-32,-24] if h==2 else [0]*3
                maxs=[16,16,32] if h==1 else [32,32,64] if h==2 else [0]*3
                hulls.append('{'+f'.clipnodes=(dclipnode_t*){n("clip" if h else "hull0")},.planes=(mplane_t*){n("planes")},.firstclipnode={p[9+h]},.lastclipnode={len(clips if h else nodes)-1},.clip_mins={nums(mins)},.clip_maxs={nums(maxs)}'+'}')
            fields.append('.hulls={'+','.join(hulls)+'}');brushes.append('{'+','.join(fields)+'}')
            mins=[int(x-1) for x in p[:3]];maxs=[int(x+1) for x in p[3:6]]
            radius=math.sqrt(sum(max(abs(a),abs(b))**2 for a,b in zip(mins,maxs)))
            descs.append('{'+f'.type=mod_brush,.numframes=2,.mins_s={nums(mins)},.maxs_s={nums(maxs)},.radius={radius.hex()}f,.brushModelData=(brush_model_data_t*)&{n("brush")}[{i}]'+'}')
        arr('brush_model_data_t',n('brush'),brushes);arr('model_t',n('models'),descs)
        c.append(f'const qrn_brush_t {n("entry")}={{QRN_BRUSH_MAGIC,{len(models)},{n("models")}}};')
        entries.append((name,f'(const byte*)&{n("entry")}',f'sizeof({n("entry")})',1))
        stats.append(dict(name=name,models=len(models),nodes=len(nodes),surfaces=len(surfaces)))
    arr('qrn_file_t','directory',['{'+json.dumps(name)+f',{p},{s},{k}'+'}' for name,p,s,k in entries])
    c.append('__attribute__((section(".header"),used)) const qrn_header_t header={"QRN1",QRN_VERSION,0,0,QRN_ABI,'+str(len(entries))+',directory,'+hex(XIP_BASE+ASSET_OFFSET)+'};')
    (work/'native.c').write_text('\n'.join(c)+'\n');(work/'raw.bin').write_bytes(raw)
    (work/'raw.S').write_text('.section .rodata.raw,"a"\n.balign 4\n.global raw\nraw:\n.incbin "'+str(work.resolve()/'raw.bin')+'"\n')
    (work/'resource.ld').write_text('SECTIONS { . = '+hex(XIP_BASE+ASSET_OFFSET)+'; .resources : { KEEP(*(.header)) *(.rodata*) } /DISCARD/ : { *(.comment*) *(.ARM.attributes*) *(.text*) *(.data*) *(.bss*) } }\n')
    cc=str(toolchain/'arm-none-eabi-gcc');flags=['-std=gnu11','-mcpu=cortex-m33','-mthumb','-mfloat-abi=softfp','-ffunction-sections','-fdata-sections','-Os','-I'+str(ROOT/'platform/macos'),'-I'+str(ROOT/'platform/rp2350/game'),'-I'+str(ROOT/'QuakeMG24/Quake'),'-I'+str(ROOT/'QuakeMG24/src'),'-include',str(ROOT/'platform/rp2350/game/config.h')]
    subprocess.run([cc,*flags,'-c',str(work/'native.c'),'-o',str(work/'native.o')],check=True)
    subprocess.run([cc,'-mcpu=cortex-m33','-mthumb','-c',str(work/'raw.S'),'-o',str(work/'raw.o')],check=True)
    subprocess.run([cc,'-mcpu=cortex-m33','-mthumb','-nostdlib','-Wl,-T,'+str(work/'resource.ld'),str(work/'native.o'),str(work/'raw.o'),'-o',str(work/'resources.elf')],check=True)
    subprocess.run([str(toolchain/'arm-none-eabi-objcopy'),'-O','binary',str(work/'resources.elf'),str(out)],check=True)
    result=bytearray(out.read_bytes());struct.pack_into('<II',result,8,len(result),zlib.crc32(result[32:]));out.write_bytes(result)
    if len(result)>SAVE_OFFSET-ASSET_OFFSET:raise ValueError(f'Native resources overflow by {len(result)-(SAVE_OFFSET-ASSET_OFFSET)} bytes')
    from verify_arm_native import validate
    checked=validate(result)
    import re
    header=['/* Generated fixed ARM resource entry addresses. */','#pragma once','#include <stdint.h>',f'#define QRN_IMAGE_BASE 0x{ASSET:08x}u',f'#define QRN_IMAGE_BYTES {len(result)}u',f'#define QRN_IMAGE_ABI 0x{checked["abi"]:08x}u']
    for entry in checked['entries']:
        symbol=re.sub('[^A-Za-z0-9]','_',entry['name']).upper()
        header.append(f'#define QRN_{symbol} ((const void*)(uintptr_t)0x{entry["address"]:08x}u)')
    out.with_suffix('.h').write_text('\n'.join(header)+'\n')
    manifest=dict(format='QRN1',base=XIP_BASE+ASSET_OFFSET,bytes=len(result),remaining=SAVE_OFFSET-ASSET_OFFSET-len(result),sha256=hashlib.sha256(result).hexdigest(),files=len(entries),levels=stats)
    out.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({k:v for k,v in manifest.items() if k!='levels'},indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('-o','--output',type=Path,required=True);p.add_argument('--toolchain',type=Path,required=True);a=p.parse_args();build(a.source,a.output,a.toolchain)
