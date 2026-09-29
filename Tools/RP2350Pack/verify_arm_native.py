#!/usr/bin/env python3
"""Validate fixed ARM pointers, native model graphs and native resource CRC."""
import argparse,struct,zlib,json
from pathlib import Path
from flash_layout import ASSET,SAVE

def validate(data):
    def check(ok,why):
        if not ok:raise ValueError(why)
    def offset(p,n=1):
        o=p-ASSET;check(o>=32 and n>=0 and o+n<=len(data),'pointer outside native image');return o
    def aligned(p,n=1,alignment=4):
        check(p%alignment==0,f'unaligned native pointer {p:#x} (requires {alignment})')
        return offset(p,n)
    def words(p,n):return struct.unpack_from('<'+'I'*n,data,aligned(p,4*n))
    check(len(data)>=32 and len(data)<=SAVE-ASSET,'native image size')
    magic,version,size,crc,abi,count,directory,base=struct.unpack_from('<4s7I',data)
    check(magic==b'QRN1' and version==1 and size==len(data) and base==ASSET,'native header')
    check(zlib.crc32(data[32:])==crc,'native checksum')
    check(0<count<=4096,'native file count');aligned(directory,count*68)
    seen=set();files=[];counts={0:0,1:0,2:0,3:0}
    for i in range(count):
        no=offset(directory+i*68,68);name,p,length,kind=struct.unpack_from('<56s3I',data,no)
        check(b'\0' in name,'file name termination');name=name.split(b'\0')[0].decode('ascii');check(name not in seen,'duplicate file');seen.add(name)
        offset(p,length);check(kind in counts,'file kind');counts[kind]+=1
        files.append(dict(name=name,address=p,bytes=length,kind=kind))
        if not kind:continue
        magic,n,models=words(p,3);check(magic=={1:0x314e4251,2:0x314e4151,3:0x314e5351}[kind] and 0<n<=255,'native model entry');aligned(models,28*n)
        for mi in range(n):
            mo=offset(models+28*mi,28);typ=data[mo+6];md=struct.unpack_from('<I',data,mo+24)[0]
            check(typ=={1:0,2:2,3:1}[kind],'model type')
            if kind==1:
                bo=aligned(md,224);first,nface,nleaf,nn,ns=struct.unpack_from('<5h',data,bo)
                check(0<=first<=ns and 0<=nface<=ns-first and nn>=0,'brush counts')
                pointers=struct.unpack_from('<9I',data,bo+12)
                for v,a in zip(pointers,(4,4,4,2,4,4,4,2,2)):aligned(v,alignment=a)
                planes,leafs,verts,edges,nodes,texinfo,surfaces,surfedges,marks=pointers
                offset(nodes,nn*28);offset(surfaces,ns*32)
                # Shared arrays need full traversal only for the root model.
                if mi==0:
                    for j in range(nn):
                        node=offset(nodes+j*28,28);pl=struct.unpack_from('<I',data,node+16)[0];aligned(pl,16);check((pl-planes)%16==0,'node plane alignment')
                        for child in struct.unpack_from('<2h',data,node+20):check(child<nn and child>=-8192,'node child')
                    for j in range(ns):
                        so=offset(surfaces+j*32,32);sample,ti,pl=struct.unpack_from('<3I',data,so+4)
                        if sample:offset(sample)
                        aligned(ti,40);aligned(pl,16);check((ti-texinfo)%40==0 and (pl-planes)%16==0,'surface references')
                        tex=words(ti+32,1)[0];aligned(tex,36)
                        for pixel in words(tex+20,4):
                            if pixel:offset(pixel)
                for h in range(4):
                    clip,plane,firstclip,lastclip=struct.unpack_from('<IIii',data,bo+48+40*h)
                    check(lastclip>=-1,'collision count');aligned(plane,16)
                    if lastclip>=0:aligned(clip,6*(lastclip+1),2)
                for j in [208,212,216,220]:
                    v=struct.unpack_from('<I',data,bo+j)[0]
                    if v:offset(v)
            elif kind==2:
                hdr=aligned(md,20);ext,mdl,st,skin,tri=struct.unpack_from('<I4i',data,hdr);offset(ext)
                m=aligned(md+mdl,56);w,h,nv,nt,ns,sync,nf,flags=struct.unpack_from('<4h4B',data,m+44)
                aligned(md+st,nv*4,2);aligned(md+skin,ns*12);aligned(ext+tri,nt*8,2);offset(md+20,nf*12)
                for j in range(ns):
                    bits,original,_=words(md+skin+j*12,3);check(not(bits&1),'skin group');offset(ext+original,w*h)
                for j in range(nf):
                    bits=words(md+20+j*12+8,1)[0];fr=bits>>1
                    if bits&1:
                        word=words(md+fr,1)[0];ng=word&511;interval=word>>9;offset(md+interval,4*ng);offset(md+fr+4,ng*12)
                        for k in range(ng):offset(ext+words(md+fr+4+k*12+8,1)[0],nv*4)
                    else:offset(ext+fr,nv*4)
            else:
                so=aligned(md,8);nf=data[so+1];offset(md+8,nf*8)
                for j in range(nf):
                    t,frame=words(md+8+j*8,2);check(t==0,'sprite frame kind');fo=aligned(frame,16);w,h=struct.unpack_from('<2h',data,fo);offset(words(frame+12,1)[0],w*h)
    return dict(format='QRN1',bytes=len(data),abi=abi,base=base,files=count,kinds=counts,entries=files)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('image',type=Path);a=p.parse_args();v=validate(a.image.read_bytes());v.pop('entries');print(json.dumps(v,indent=2))
