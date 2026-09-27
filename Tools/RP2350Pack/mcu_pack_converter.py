#!/usr/bin/env python3
"""Portable Python implementation of the MG24 Quake PAK conversion pipeline."""
from __future__ import annotations
import argparse, json, struct
from collections import OrderedDict
from pathlib import Path
from mdl_converter import convert_mdl

PAK_HDR=struct.Struct('<4sII'); PAK_ENT=struct.Struct('<56sII')
BSP_VERSION=29; BSP_LUMPS=15; BSP_HDR_SIZE=4+BSP_LUMPS*8
LUMP_NAMES=['entities','planes','textures','vertices','visibility','nodes','texinfo','faces','lighting','clipnodes','leafs','marksurfaces','edges','surfedges','models']
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
    names=list(files); doff=12; dlen=64*len(names); pos=doff+dlen; directory=[]; payload=bytearray()
    for name in names:
        data=files[name]; enc=name.encode('ascii')
        if len(enc)>55: raise ValueError(f'PAK name too long: {name}')
        directory.append(PAK_ENT.pack(enc.ljust(56,b'\0'),pos,len(data))); payload.extend(data); pos+=len(data)
    image=bytearray(PAK_HDR.pack(b'PACK',doff,dlen)); image.extend(b''.join(directory)); image.extend(payload); Path(path).write_bytes(image); return len(image)

def chunks(wav):
    if len(wav)<12 or wav[:4]!=b'RIFF' or wav[8:12]!=b'WAVE': raise ValueError('not RIFF/WAVE')
    p=12
    while p+8<=len(wav):
        tag=wav[p:p+4]; n=struct.unpack_from('<I',wav,p+4)[0]; data=p+8; end=data+n
        if end>len(wav): break
        yield tag,wav[data:end],p; p=end+(n&1)

def wav_info(wav):
    info={'loopstart':-1}
    for tag,data,_ in chunks(wav):
        if tag==b'fmt ' and len(data)>=16:
            form,ch,rate,byterate,block,bits=struct.unpack_from('<HHIIHH',data); info.update(format=form,channels=ch,rate=rate,byte_rate=byterate,bits=bits,block_align=block)
        elif tag==b'data': info['data_bytes']=len(data)
        elif tag==b'cue ' and len(data)>=28: info['loopstart']=struct.unpack_from('<I',data,24)[0]
    if 'rate' not in info or 'data_bytes' not in info: raise ValueError('missing WAV fmt/data')
    bytes_per_sample=info['channels']*(info['bits']//8); info['samples']=info['data_bytes']//bytes_per_sample; info['duration_seconds']=info['samples']/info['rate']; return info

def convert_wav(wav):
    wi=wav_info(wav); pcm=None
    for tag,data,_ in chunks(wav):
        if tag==b'data': pcm=data; break
    ch=wi['channels'];rate=wi['rate'];width=wi['bits']//8;loopstart=wi['loopstart']
    if wi['format']!=1 or ch!=1 or width not in (1,2): raise ValueError('unsupported WAV format')
    insamples=len(pcm)//width; scale=rate/11025.0; outlen=int(insamples/scale)
    if loopstart!=-1: loopstart=int(loopstart/scale)
    out=bytearray(outlen); frac=0; fracstep=int(scale*256)
    for i in range(outlen):
        src=min(frac>>8,insamples-1); frac+=fracstep
        sample=pcm[src]-128 if width==1 else struct.unpack_from('<h',pcm,src*2)[0]>>8; out[i]=sample&255
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
    if len(blob)<4:return {'count':0,'skies':[]}
    n=struct.unpack_from('<i',blob)[0]; skies=[]
    if n<0 or 4+4*n>len(blob): return {'count':0,'skies':[],'error':'bad miptex directory'}
    for i in range(n):
        rel=struct.unpack_from('<i',blob,4+4*i)[0]
        if rel>=0 and rel+40<=len(blob):
            raw,w,h,*_=struct.unpack_from('<16s6I',blob,rel); name=raw.split(b'\0',1)[0].decode('latin1')
            if 'sky' in name.lower(): skies.append({'index':i,'name':name,'width':w,'height':h})
    return {'count':n,'skies':skies}

def analyze_bsp(name,data):
    lumps=parse_bsp(data); info=[]
    for i,(off,n,blob) in enumerate(lumps):
        x={'index':i,'name':LUMP_NAMES[i],'old_offset':off,'old_bytes':n,'action':'copy'}
        if i==2:x['textures']=texture_info(blob)
        info.append(x)
    return lumps,info,{'faces':lumps[7][1]//20,'nodes':lumps[5][1]//24,'leafs':lumps[10][1]//28}

def repack_bsp_copy(data):
    lumps=parse_bsp(data); out=bytearray(BSP_HDR_SIZE); struct.pack_into('<I',out,0,BSP_VERSION)
    for i in LUMP_ORDER:
        while len(out)&3:out.append(0)
        off=len(out); blob=lumps[i][2];out.extend(blob);struct.pack_into('<II',out,4+i*8,off,len(blob))
    return bytes(out)

def projected_bsp_size(data,mnode_size,mleaf_size):
    lumps=parse_bsp(data);faces=lumps[7][1]//20;nodes=lumps[5][1]//24;leafs=lumps[10][1]//28;nodeblock=4+2*faces+mnode_size*nodes+mleaf_size*leafs;pos=BSP_HDR_SIZE
    for i in LUMP_ORDER: pos=align4(pos);pos+=0 if i==10 else nodeblock if i==5 else lumps[i][1]
    return pos,nodeblock

def convert(files,bsp_mode,mnode_size,mleaf_size):
    out=OrderedDict();manifest={'files':[],'totals':{'input':0,'output':0},'notes':[]};mdl_in=mdl_out=mdl_count=0
    snd={'files':0,'source_file_bytes':0,'source_pcm_bytes':0,'converted_bytes':0,'duration_seconds':0.0,'source_rates':{},'source_bits':{},'errors':0}
    for name,data in files.items():
        low=name.lower();result=data;kind='copy';extra={}
        if low.startswith('sound') and low.endswith('.wav'):
            snd['files']+=1;snd['source_file_bytes']+=len(data)
            try:
                wi=wav_info(data);result=convert_wav(data);kind='wav_11025_s8';snd['source_pcm_bytes']+=wi['data_bytes'];snd['converted_bytes']+=len(result);snd['duration_seconds']+=wi['duration_seconds'];snd['source_rates'][str(wi['rate'])]=snd['source_rates'].get(str(wi['rate']),0)+1;snd['source_bits'][str(wi['bits'])]=snd['source_bits'].get(str(wi['bits']),0)+1;extra['wav']=wi
            except Exception as e:kind='wav_error_passthrough';extra['error']=str(e);snd['converted_bytes']+=len(data);snd['errors']+=1
        elif low.endswith('.bsp'):
            try:
                lumps,linfo,counts=analyze_bsp(name,data);projected,nodeblock=projected_bsp_size(data,mnode_size,mleaf_size);extra={'lumps':linfo,'counts':counts,'projected_mg24_bytes':projected,'projected_nodeblock_bytes':nodeblock,'mnode_size':mnode_size,'mleaf_size':mleaf_size}
                if bsp_mode=='copy':result=repack_bsp_copy(data);kind='bsp_repacked_copy'
                else:kind='bsp_analyzed_passthrough'
            except Exception as e:kind='bsp_error_passthrough';extra['error']=str(e)
        elif low.endswith('.mdl'):
            mdl_count+=1;mdl_in+=len(data)
            try: result,meta=convert_mdl(data);kind='mdl_mg24_memory_ready';extra['mdl']=meta;mdl_out+=len(result)
            except Exception as e: kind='mdl_error_passthrough';extra['error']=str(e);mdl_out+=len(data)
        out[name]=result;rec={'name':name,'kind':kind,'input_bytes':len(data),'output_bytes':len(result),'delta':len(result)-len(data)};rec.update(extra);manifest['files'].append(rec);manifest['totals']['input']+=len(data);manifest['totals']['output']+=len(result)
    snd['pcm_11025_payload_bytes']=round(snd['duration_seconds']*11025);snd['pcm_8000_payload_bytes']=round(snd['duration_seconds']*8000);snd['pcm_5512_payload_bytes']=round(snd['duration_seconds']*5512.5);snd['ima_adpcm_11025_payload_bytes']=(snd['pcm_11025_payload_bytes']+1)//2
    manifest['sound_totals']=snd;manifest['mdl_totals']={'files':mdl_count,'input_bytes':mdl_in,'output_bytes':mdl_out,'delta_bytes':mdl_out-mdl_in}
    manifest['notes'].append('Sound conversion target is mono signed 8-bit PCM at 11025 Hz; lower-rate and 4-bit ADPCM figures are footprint projections only.')
    manifest['notes'].append('RP2350 keeps native BSP node/leaf data; MG24 node/leaf projection remains comparison data only.')
    return out,manifest

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('input',type=Path);ap.add_argument('output',type=Path);ap.add_argument('--manifest',type=Path);ap.add_argument('--bsp-mode',choices=('analyze','copy'),default='analyze');ap.add_argument('--mnode-size',type=int,default=24);ap.add_argument('--mleaf-size',type=int,default=24);args=ap.parse_args();files=read_pak(args.input);converted,manifest=convert(files,args.bsp_mode,args.mnode_size,args.mleaf_size);args.output.parent.mkdir(parents=True,exist_ok=True);pakbytes=write_pak(converted,args.output);manifest['pak_output_bytes']=pakbytes
    if args.manifest:args.manifest.parent.mkdir(parents=True,exist_ok=True);args.manifest.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(f'Python MCUPackConverter: {len(files)} files, {manifest["totals"]["input"]:,} -> {manifest["totals"]["output"]:,} payload bytes');print(f'PAK output: {pakbytes:,} bytes');mt=manifest['mdl_totals'];print(f'MDL conversion: {mt["files"]} files, {mt["input_bytes"]:,} -> {mt["output_bytes"]:,} bytes ({mt["delta_bytes"]:+,})');s=manifest['sound_totals'];print(f'Sound conversion: {s["files"]} WAV files, {s["source_file_bytes"]:,} -> {s["converted_bytes"]:,} bytes, {s["duration_seconds"]:.1f} s total, target 11025 Hz mono 8-bit')
if __name__=='__main__':main()
