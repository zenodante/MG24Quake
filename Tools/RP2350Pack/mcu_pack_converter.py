#!/usr/bin/env python3
"""Portable Python implementation of the MG24 Quake PAK conversion pipeline.

This intentionally uses explicit little-endian binary layouts rather than host C
structs, so output is independent of macOS/Linux pointer size and alignment.

Implemented faithfully from Tools/MCUPackConverter:
  * PAK read/write
  * WAV -> MG24 11025-Hz signed-8-bit payload (length, loopstart, samples)
  * BSP29 repack in the author's lump load order
  * BSP texture/sky discovery and manifesting
  * exact accounting of the author's special node+leaf replacement

The original C node/leaf replacement serializes target-specific mnode_t/mleaf_t
structures whose exact layout is controlled by the firmware headers.  Until the
Python serializer is verified byte-for-byte against those target definitions,
this tool defaults to --bsp-mode analyze: it computes the converted size/layout
without emitting an incompatible BSP.  --bsp-mode copy emits a canonical
4-byte-aligned BSP with unchanged lumps and is useful for pipeline testing.

Alias MDL conversion is likewise reported but passed through for now.  This is
preferable to silently generating a host-ABI-dependent representation.
"""
from __future__ import annotations
import argparse, json, math, struct
from collections import OrderedDict
from pathlib import Path

PAK_HDR=struct.Struct('<4sII'); PAK_ENT=struct.Struct('<56sII')
BSP_VERSION=29; BSP_LUMPS=15; BSP_HDR_SIZE=4+BSP_LUMPS*8
LUMP_NAMES=['entities','planes','textures','vertices','visibility','nodes','texinfo','faces','lighting','clipnodes','leafs','marksurfaces','edges','surfedges','models']
# author's Convert_BrushModel order
LUMP_ORDER=[3,12,13,2,8,1,6,7,11,4,10,5,9,0,14]

def align4(n): return (n+3)&~3

def read_pak(path):
    b=Path(path).read_bytes(); magic,doff,dlen=PAK_HDR.unpack_from(b)
    if magic!=b'PACK' or dlen%64 or doff+dlen>len(b): raise ValueError('invalid PAK')
    out=OrderedDict()
    for p in range(doff,doff+dlen,64):
        raw,off,n=PAK_ENT.unpack_from(b,p); name=raw.split(b'\0',1)[0].decode('ascii')
        if off+n>len(b): raise ValueError(f'{name}: outside PAK')
        out[name]=b[off:off+n]
    return out

def write_pak(files,path):
    # Match original converter: directory immediately after 12-byte header.
    names=list(files); doff=12; dlen=64*len(names); pos=doff+dlen
    directory=[]; payload=bytearray()
    for name in names:
        data=files[name]; enc=name.encode('ascii')
        if len(enc)>55: raise ValueError(f'PAK name too long: {name}')
        directory.append(PAK_ENT.pack(enc.ljust(56,b'\0'),pos,len(data)))
        payload.extend(data); pos+=len(data)
    image=bytearray(PAK_HDR.pack(b'PACK',doff,dlen)); image.extend(b''.join(directory)); image.extend(payload)
    Path(path).write_bytes(image); return len(image)

def chunks(wav):
    if len(wav)<12 or wav[:4]!=b'RIFF' or wav[8:12]!=b'WAVE': raise ValueError('not RIFF/WAVE')
    p=12
    while p+8<=len(wav):
        tag=wav[p:p+4]; n=struct.unpack_from('<I',wav,p+4)[0]; data=p+8; end=data+n
        if end>len(wav): break
        yield tag,wav[data:end],p
        p=end+(n&1)

def convert_wav(wav):
    fmt=None; pcm=None; loopstart=-1
    for tag,data,_ in chunks(wav):
        if tag==b'fmt ' and len(data)>=16:
            form,ch,rate,_,_,bits=struct.unpack_from('<HHIIHH',data)
            if form!=1: raise ValueError('non-PCM WAV')
            fmt=(ch,rate,bits//8)
        elif tag==b'data': pcm=data
        elif tag==b'cue ' and len(data)>=28:
            # Original GetWavinfo reads sample offset at RIFF chunk +32.
            loopstart=struct.unpack_from('<I',data,24)[0]
    if not fmt or pcm is None: raise ValueError('missing WAV fmt/data')
    ch,rate,width=fmt
    if ch!=1: raise ValueError('original converter expects mono sound')
    if width not in (1,2): raise ValueError('unsupported WAV width')
    insamples=len(pcm)//width; scale=rate/11025.0; outlen=int(insamples/scale)
    if loopstart!=-1: loopstart=int(loopstart/scale)
    out=bytearray(outlen)
    frac=0; fracstep=int(scale*256)
    for i in range(outlen):
        src=frac>>8; frac+=fracstep
        if src>=insamples: src=insamples-1
        if width==1: sample=pcm[src]-128
        else: sample=struct.unpack_from('<h',pcm,src*2)[0]>>8
        out[i]=sample&255
    return struct.pack('<ii',outlen,loopstart)+out

def parse_bsp(data):
    if len(data)<BSP_HDR_SIZE: raise ValueError('truncated BSP')
    ver=struct.unpack_from('<I',data)[0]
    if ver!=BSP_VERSION: raise ValueError(f'BSP version {ver}')
    lumps=[]
    for i in range(BSP_LUMPS):
        off,n=struct.unpack_from('<II',data,4+i*8)
        if off+n>len(data): raise ValueError(f'{LUMP_NAMES[i]} outside BSP')
        lumps.append((off,n,data[off:off+n]))
    return lumps

def texture_info(blob):
    if len(blob)<4: return {'count':0,'skies':[]}
    n=struct.unpack_from('<i',blob)[0]
    if n<0 or 4+4*n>len(blob): return {'count':0,'skies':[],'error':'bad miptex directory'}
    skies=[]
    for i in range(n):
        rel=struct.unpack_from('<i',blob,4+4*i)[0]
        if rel<0 or rel+40>len(blob): continue
        raw,w,h,*_=struct.unpack_from('<16s6I',blob,rel); name=raw.split(b'\0',1)[0].decode('latin1')
        if 'sky' in name.lower(): skies.append({'index':i,'name':name,'width':w,'height':h})
    return {'count':n,'skies':skies}

def analyze_bsp(name,data):
    lumps=parse_bsp(data); info=[]
    for i,(off,n,blob) in enumerate(lumps):
        x={'index':i,'name':LUMP_NAMES[i],'old_offset':off,'old_bytes':n,'action':'copy'}
        if i==2: x['textures']=texture_info(blob)
        if i==10: x['action']='join_into_nodes'; x['projected_bytes']=0
        elif i==5:
            # C output: 4-byte counts + 2 bytes/surface surfNodeIndex + target mnode/mleaf arrays.
            # Record the exact source counts; target structure sizes are deliberately explicit CLI inputs.
            x['action']='replace_nodes_and_leafs'
        info.append(x)
    faces=lumps[7][1]//20; nodes=lumps[5][1]//24; leafs=lumps[10][1]//28
    return lumps,info,{'faces':faces,'nodes':nodes,'leafs':leafs}

def repack_bsp_copy(data):
    lumps=parse_bsp(data); out=bytearray(BSP_HDR_SIZE); struct.pack_into('<I',out,0,BSP_VERSION)
    for i in LUMP_ORDER:
        while len(out)&3: out.append(0)
        off=len(out); blob=lumps[i][2]; out.extend(blob)
        struct.pack_into('<II',out,4+i*8,off,len(blob))
    return bytes(out)

def projected_bsp_size(data,mnode_size,mleaf_size):
    lumps=parse_bsp(data); faces=lumps[7][1]//20; nodes=lumps[5][1]//24; leafs=lumps[10][1]//28
    nodeblock=4+2*faces+mnode_size*nodes+mleaf_size*leafs
    pos=BSP_HDR_SIZE
    for i in LUMP_ORDER:
        pos=align4(pos)
        if i==10: n=0
        elif i==5: n=nodeblock
        else: n=lumps[i][1]
        pos+=n
    return pos,nodeblock

def convert(files,bsp_mode,mnode_size,mleaf_size):
    out=OrderedDict(); manifest={'files':[],'totals':{'input':0,'output':0},'notes':[]}
    for name,data in files.items():
        low=name.lower(); result=data; kind='copy'; extra={}
        if low.startswith('sound') and low.endswith('.wav'):
            try: result=convert_wav(data); kind='wav_11025_s8'
            except Exception as e: kind='wav_error_passthrough'; extra['error']=str(e)
        elif low.endswith('.bsp'):
            try:
                lumps,linfo,counts=analyze_bsp(name,data); projected,nodeblock=projected_bsp_size(data,mnode_size,mleaf_size)
                extra={'lumps':linfo,'counts':counts,'projected_mg24_bytes':projected,'projected_nodeblock_bytes':nodeblock,
                       'mnode_size':mnode_size,'mleaf_size':mleaf_size}
                if bsp_mode=='copy': result=repack_bsp_copy(data); kind='bsp_repacked_copy'
                else: kind='bsp_analyzed_passthrough'
            except Exception as e: kind='bsp_error_passthrough'; extra['error']=str(e)
        elif low.endswith('.mdl'):
            kind='mdl_passthrough_pending_python_alias_converter'
        out[name]=result
        rec={'name':name,'kind':kind,'input_bytes':len(data),'output_bytes':len(result),'delta':len(result)-len(data)}; rec.update(extra)
        manifest['files'].append(rec); manifest['totals']['input']+=len(data); manifest['totals']['output']+=len(result)
    manifest['notes'].append('BSP projected sizes reproduce the C converter allocation formula; final target mnode_t/mleaf_t sizes must match firmware definitions.')
    manifest['notes'].append('MDL serializer is intentionally pass-through until the alias-model target layout is translated and verified.')
    return out,manifest

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input',type=Path); ap.add_argument('output',type=Path)
    ap.add_argument('--manifest',type=Path)
    ap.add_argument('--bsp-mode',choices=('analyze','copy'),default='analyze')
    ap.add_argument('--mnode-size',type=int,default=24,help='target sizeof(mnode_t), used for MG24 BSP size projection')
    ap.add_argument('--mleaf-size',type=int,default=24,help='target sizeof(mleaf_t), used for MG24 BSP size projection')
    args=ap.parse_args(); files=read_pak(args.input); converted,manifest=convert(files,args.bsp_mode,args.mnode_size,args.mleaf_size)
    args.output.parent.mkdir(parents=True,exist_ok=True); pakbytes=write_pak(converted,args.output); manifest['pak_output_bytes']=pakbytes
    if args.manifest:
        args.manifest.parent.mkdir(parents=True,exist_ok=True); args.manifest.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(f'Python MCUPackConverter: {len(files)} files, {manifest["totals"]["input"]:,} -> {manifest["totals"]["output"]:,} payload bytes')
    print(f'PAK output: {pakbytes:,} bytes')
    bsp=[x for x in manifest['files'] if '.bsp' in x['name'].lower() and 'projected_mg24_bytes' in x]
    if bsp:
        raw=sum(x['input_bytes'] for x in bsp); proj=sum(x['projected_mg24_bytes'] for x in bsp)
        print(f'BSP MG24 projection: {raw:,} -> {proj:,} bytes ({proj-raw:+,})')
    pending=sum(1 for x in manifest['files'] if x['kind'].startswith('mdl_passthrough'))
    if pending: print(f'NOTE: {pending} MDL files are currently pass-through pending alias-model serializer.')

if __name__=='__main__': main()
