#!/usr/bin/env python3
"""Portable Python implementation of the RP2350 Quake PAK conversion pipeline."""
from __future__ import annotations
import argparse, json, struct
from collections import OrderedDict
from pathlib import Path
from mdl_converter import convert_mdl
PAK_HDR=struct.Struct('<4sII'); PAK_ENT=struct.Struct('<56sII'); BSP_VERSION=29; BSP_LUMPS=15; BSP_HDR_SIZE=124
LUMP_NAMES=['entities','planes','textures','vertices','visibility','nodes','texinfo','faces','lighting','clipnodes','leafs','marksurfaces','edges','surfedges','models'];LUMP_ORDER=[3,12,13,2,8,1,6,7,11,4,10,5,9,0,14]
IMA_STEP=(7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767)
IMA_INDEX=(-1,-1,-1,-1,2,4,6,8); ADPCM_BLOCK_SAMPLES=256; ADPCM_HDR=struct.Struct('<4sIIHH'); ADPCM_BLOCK_HDR=struct.Struct('<hBBH')
def align4(n):return(n+3)&~3
def read_pak(path):
 b=Path(path).read_bytes();magic,doff,dlen=PAK_HDR.unpack_from(b)
 if magic!=b'PACK' or dlen%64 or doff+dlen>len(b):raise ValueError('invalid PAK')
 out=OrderedDict()
 for p in range(doff,doff+dlen,64):
  raw,off,n=PAK_ENT.unpack_from(b,p);name=raw.split(b'\0',1)[0].decode('ascii');out[name]=b[off:off+n]
 return out
def write_pak(files,path):
 names=list(files);doff=12;dlen=64*len(names);pos=doff+dlen;directory=[];payload=bytearray()
 for name in names:
  data=files[name];enc=name.encode('ascii');directory.append(PAK_ENT.pack(enc.ljust(56,b'\0'),pos,len(data)));payload.extend(data);pos+=len(data)
 image=bytearray(PAK_HDR.pack(b'PACK',doff,dlen));image.extend(b''.join(directory));image.extend(payload);Path(path).write_bytes(image);return len(image)
def chunks(wav):
 if len(wav)<12 or wav[:4]!=b'RIFF' or wav[8:12]!=b'WAVE':raise ValueError('not RIFF/WAVE')
 p=12
 while p+8<=len(wav):
  tag=wav[p:p+4];n=struct.unpack_from('<I',wav,p+4)[0];data=p+8;end=data+n
  if end>len(wav):break
  yield tag,wav[data:end],p;p=end+(n&1)
def wav_info(wav):
 info={'loopstart':-1}
 for tag,data,_ in chunks(wav):
  if tag==b'fmt ' and len(data)>=16:
   form,ch,rate,byterate,block,bits=struct.unpack_from('<HHIIHH',data);info.update(format=form,channels=ch,rate=rate,byte_rate=byterate,bits=bits,block_align=block)
  elif tag==b'data':info['data_bytes']=len(data)
  elif tag==b'cue ' and len(data)>=28:info['loopstart']=struct.unpack_from('<I',data,24)[0]
 if 'rate' not in info or 'data_bytes' not in info:raise ValueError('missing WAV fmt/data')
 bps=info['channels']*(info['bits']//8);info['samples']=info['data_bytes']//bps;info['duration_seconds']=info['samples']/info['rate'];return info
def wav_to_pcm11025(wav):
 wi=wav_info(wav);pcm=next(data for tag,data,_ in chunks(wav) if tag==b'data');ch=wi['channels'];rate=wi['rate'];width=wi['bits']//8
 if wi['format']!=1 or ch!=1 or width not in(1,2):raise ValueError('unsupported WAV format')
 insamples=len(pcm)//width;scale=rate/11025.0;outlen=int(insamples/scale);loop=-1 if wi['loopstart']==-1 else int(wi['loopstart']/scale);out=[];frac=0;step=int(scale*256)
 for _ in range(outlen):
  src=min(frac>>8,insamples-1);frac+=step;v=pcm[src]-128 if width==1 else struct.unpack_from('<h',pcm,src*2)[0]>>8;out.append(max(-128,min(127,v)))
 return out,loop,wi
def ima_nibble(sample,pred,index):
 step=IMA_STEP[index];diff=sample-pred;code=8 if diff<0 else 0;d=abs(diff);delta=step>>3
 if d>=step:code|=4;d-=step;delta+=step
 if d>=step>>1:code|=2;d-=step>>1;delta+=step>>1
 if d>=step>>2:code|=1;delta+=step>>2
 pred=pred-delta if code&8 else pred+delta;pred=max(-32768,min(32767,pred));index=max(0,min(88,index+IMA_INDEX[code&7]));return code,pred,index
def convert_wav(wav):
 pcm8,loop,wi=wav_to_pcm11025(wav);pcm16=[x<<8 for x in pcm8];out=bytearray(ADPCM_HDR.pack(b'QAD1',len(pcm16),0xffffffff if loop<0 else loop,ADPCM_BLOCK_SAMPLES,0));blocks=0
 for start in range(0,len(pcm16),ADPCM_BLOCK_SAMPLES):
  block=pcm16[start:start+ADPCM_BLOCK_SAMPLES];pred=block[0];index=0;out.extend(ADPCM_BLOCK_HDR.pack(pred,index,0,len(block)));nibs=[]
  for sample in block[1:]:code,pred,index=ima_nibble(sample,pred,index);nibs.append(code)
  for i in range(0,len(nibs),2):out.append(nibs[i]|((nibs[i+1] if i+1<len(nibs) else 0)<<4))
  blocks+=1
 return bytes(out),{'samples':len(pcm16),'loopstart':loop,'blocks':blocks,'block_samples':ADPCM_BLOCK_SAMPLES,'source':wi}
def parse_bsp(data):
 if len(data)<BSP_HDR_SIZE:raise ValueError('truncated BSP')
 if struct.unpack_from('<I',data)[0]!=BSP_VERSION:raise ValueError('bad BSP version')
 lumps=[]
 for i in range(BSP_LUMPS):off,n=struct.unpack_from('<II',data,4+i*8);lumps.append((off,n,data[off:off+n]))
 return lumps
def projected_bsp_size(data,mnode_size,mleaf_size):
 lumps=parse_bsp(data);faces=lumps[7][1]//20;nodes=lumps[5][1]//24;leafs=lumps[10][1]//28;nodeblock=4+2*faces+mnode_size*nodes+mleaf_size*leafs;pos=BSP_HDR_SIZE
 for i in LUMP_ORDER:pos=align4(pos);pos+=0 if i==10 else nodeblock if i==5 else lumps[i][1]
 return pos,nodeblock
def convert(files,bsp_mode,mnode_size,mleaf_size):
 out=OrderedDict();manifest={'files':[],'totals':{'input':0,'output':0},'notes':[]};mdl_in=mdl_out=mdl_count=0;snd={'files':0,'source_file_bytes':0,'converted_bytes':0,'duration_seconds':0.0,'source_rates':{},'source_bits':{},'samples':0,'blocks':0,'errors':0}
 for name,data in files.items():
  low=name.lower();result=data;kind='copy';extra={}
  if low.startswith('sound') and low.endswith('.wav'):
   snd['files']+=1;snd['source_file_bytes']+=len(data)
   try:
    result,meta=convert_wav(data);wi=meta['source'];kind='sound_qad1_ima_adpcm';extra['sound']=meta;snd['converted_bytes']+=len(result);snd['duration_seconds']+=wi['duration_seconds'];snd['samples']+=meta['samples'];snd['blocks']+=meta['blocks'];snd['source_rates'][str(wi['rate'])]=snd['source_rates'].get(str(wi['rate']),0)+1;snd['source_bits'][str(wi['bits'])]=snd['source_bits'].get(str(wi['bits']),0)+1
   except Exception as e:kind='sound_error_passthrough';extra['error']=str(e);snd['converted_bytes']+=len(data);snd['errors']+=1
  elif low.endswith('.bsp'):
   try:
    projected,nodeblock=projected_bsp_size(data,mnode_size,mleaf_size);extra={'projected_mg24_bytes':projected,'projected_nodeblock_bytes':nodeblock};kind='bsp_native_passthrough'
   except Exception as e:kind='bsp_error_passthrough';extra['error']=str(e)
  elif low.endswith('.mdl'):
   mdl_count+=1;mdl_in+=len(data)
   try:result,meta=convert_mdl(data);kind='mdl_mg24_memory_ready';extra['mdl']=meta;mdl_out+=len(result)
   except Exception as e:kind='mdl_error_passthrough';extra['error']=str(e);mdl_out+=len(data)
  out[name]=result;rec={'name':name,'kind':kind,'input_bytes':len(data),'output_bytes':len(result),'delta':len(result)-len(data)};rec.update(extra);manifest['files'].append(rec);manifest['totals']['input']+=len(data);manifest['totals']['output']+=len(result)
 manifest['sound_totals']=snd;manifest['mdl_totals']={'files':mdl_count,'input_bytes':mdl_in,'output_bytes':mdl_out,'delta_bytes':mdl_out-mdl_in};manifest['notes']+=['Sound is QAD1 block IMA ADPCM: 11025 Hz mono, 256 samples/block, independent predictor/index per block for bounded random access and looping.','RP2350 keeps native BSP node/leaf data; MG24 node/leaf format is rejected.'];return out,manifest
def main():
 ap=argparse.ArgumentParser();ap.add_argument('input',type=Path);ap.add_argument('output',type=Path);ap.add_argument('--manifest',type=Path);ap.add_argument('--bsp-mode',default='analyze');ap.add_argument('--mnode-size',type=int,default=24);ap.add_argument('--mleaf-size',type=int,default=24);args=ap.parse_args();files=read_pak(args.input);converted,m=convert(files,args.bsp_mode,args.mnode_size,args.mleaf_size);args.output.parent.mkdir(parents=True,exist_ok=True);m['pak_output_bytes']=write_pak(converted,args.output)
 if args.manifest:args.manifest.write_text(json.dumps(m,indent=2))
 s=m['sound_totals'];print(f'Python converter: {m["totals"]["input"]:,} -> {m["totals"]["output"]:,} bytes');print(f'Sound QAD1 IMA ADPCM: {s["files"]} files, {s["source_file_bytes"]:,} -> {s["converted_bytes"]:,} bytes, {s["blocks"]} blocks')
if __name__=='__main__':main()
