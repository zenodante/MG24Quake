#!/usr/bin/env python3
"""MG24 alias-model (.mdl) converter in pure Python.

Translates the active NO_MINIMIZE=0 MCUPackConverter alias path into explicit
little-endian serialization.  It supports Quake MDL v6 single skins, single
frames and frame groups used by the shareware PAK.  Like the C converter it:
- packs stverts into one 32-bit word;
- packs mtriangle_t into two 32-bit words;
- creates per-triangle column-offset tables;
- stores only triangle-covered skin pixels (one copy per triangle/skin);
- aligns offset tables and frame vertex arrays to four bytes;
- emits the relocatable IDPM ('memory ready') alias representation.

No host pointers, C bitfields or native alignment are used.
"""
from __future__ import annotations
import struct

IDPO=0x4F504449; IDPM=0x4D504449; VERSION=6
DISK_MDL=struct.Struct('<ii3f3ff3f8if') # 84 bytes
DSTVERT=struct.Struct('<iii'); DTRI=struct.Struct('<4i')
TRIVERT=struct.Struct('<4B')

def a4(n): return (n+3)&~3

def line_y(x0,y0,x1,y1,x):
    minx,maxx=sorted((x0,x1)); x=max(minx,min(maxx,x)); dx=abs(x1-x0); sx=1 if x0<x1 else -1
    dy=-abs(y1-y0); sy=1 if y0<y1 else -1; err=dx+dy; found=False; first=-1; second=-2
    while True:
        if x0==x and not found: first=second=y0; found=True
        elif x0==x and found: second=y0
        elif found and x0!=x: return min(first,second),max(first,second)
        if x0==x1 and y0==y1: break
        e2=2*err
        if e2>=dy:
            if x0==x1: break
            err+=dy; x0+=sx
        if e2<=dx:
            if y0==y1: break
            err+=dx; y0+=sy
    second=y0
    if not found: first=y0
    return min(first,second),max(first,second)

def bounds(a,b,c,d): return min(a,b,c,d),max(a,b,c,d)

def columns(coords):
    ss=[p[0] for p in coords]; ts=[p[1] for p in coords]; mn,mx=min(ss),max(ss)
    left=ss.index(mn); right=ss.index(mx); central=3-left-right
    if central==3: left,right,central=1,0,2
    out=[]
    if min(ts)==max(ts):
        for s in range(mn,mx+1): out.append((s,max(ts),max(ts)))
    elif mn!=mx:
        if coords[central][0]==mx:
            for s in range(mn,mx+1):
                a,b=line_y(mn,coords[left][1],mx,coords[right][1],s); c,d=line_y(mn,coords[left][1],mx,coords[central][1],s)
                lo,hi=bounds(a,b,c,d); out.append((s,lo,hi))
        elif coords[central][0]==mn:
            for s in range(mn,mx+1):
                a,b=line_y(mn,coords[central][1],mx,coords[right][1],s); c,d=line_y(mn,coords[left][1],mx,coords[right][1],s)
                lo,hi=bounds(a,b,c,d); out.append((s,lo,hi))
        else:
            for s in range(mn,coords[central][0]):
                a,b=line_y(mn,coords[left][1],coords[central][0],coords[central][1],s); c,d=line_y(mn,coords[left][1],mx,coords[right][1],s)
                lo,hi=bounds(a,b,c,d); out.append((s,lo,hi))
            for s in range(coords[central][0],mx+1):
                a,b=line_y(coords[central][0],coords[central][1],mx,coords[right][1],s); c,d=line_y(mn,coords[left][1],mx,coords[right][1],s)
                lo,hi=bounds(a,b,c,d); out.append((s,lo,hi))
    else:
        out.append((mn,min(ts),max(ts)))
    return out

def coords_for(tri,st,skinw):
    front,inds=tri; r=[]
    for idx in inds:
        seam,s,t=st[idx]; r.append((s + (skinw//2 if (not front and seam) else 0),t))
    return r

def skin_triangle_bytes(skin,w,h,coords):
    out=bytearray()
    for s,lo,hi in columns(coords):
        if not (0<=s<w) or lo<0 or hi>=h: raise ValueError(f'skin triangle outside {w}x{h}: s={s}, t={lo}..{hi}')
        out.extend(skin[lo*w+s:(hi+1)*w+s:w])
    return bytes(out)

def offset_table(coords,offset):
    cols=columns(coords); mn=min(p[0] for p in coords); max_t=max(p[1] for p in coords); start=offset; vals=[]; size=0
    for s,lo,hi in cols:
        vals.append(offset-lo+max_t); n=hi-lo+1; offset+=n; size+=n
    if offset>65535: raise ValueError('triangle skin stream offset exceeds 65535')
    return vals,mn,max_t,start,size,a4(offset)

def parse_source(data):
    if len(data)<84: raise ValueError('truncated MDL')
    v=DISK_MDL.unpack_from(data); ident,ver=v[:2]
    if ident!=IDPO or ver!=VERSION: raise ValueError(f'not Quake MDL v6: {ident:#x}/{ver}')
    numskins,sw,sh,nv,nt,nf,synctype,flags=v[12:20]; p=84; skins=[]
    for _ in range(numskins):
        typ=struct.unpack_from('<i',data,p)[0]; p+=4
        if typ!=0: raise ValueError('skin groups are unsupported by original active MG24 converter path')
        n=sw*sh; skins.append(data[p:p+n]); p+=n
    st=[]
    for _ in range(nv): seam,s,t=DSTVERT.unpack_from(data,p); p+=12; st.append((1 if seam else 0,s,t))
    tris=[]
    for _ in range(nt): front,a,b,c=DTRI.unpack_from(data,p); p+=16; tris.append((front,(a,b,c)))
    frames=[]
    for _ in range(nf):
        typ=struct.unpack_from('<i',data,p)[0]; p+=4
        if typ==0:
            bmin=data[p:p+4]; bmax=data[p+4:p+8]; name=data[p+8:p+24].split(b'\0',1)[0].decode('latin1'); p+=24
            verts=data[p:p+nv*4]; p+=nv*4; frames.append(('single',bmin,bmax,name,verts))
        else:
            ng=struct.unpack_from('<i',data,p)[0]; bmin=data[p+4:p+8]; bmax=data[p+8:p+12]; p+=12
            intervals=list(struct.unpack_from('<'+'f'*ng,data,p)); p+=4*ng; group=[]
            for __ in range(ng):
                gbmin=data[p:p+4]; gbmax=data[p+4:p+8]; name=data[p+8:p+24].split(b'\0',1)[0].decode('latin1'); p+=24
                verts=data[p:p+nv*4]; p+=nv*4; group.append((gbmin,gbmax,name,verts))
            frames.append(('group',bmin,bmax,intervals,group))
    return v,skins,st,tris,frames

def convert_mdl(data):
    v,skins,st,tris,frames=parse_source(data); numskins,sw,sh,nv,nt,nf,synctype,flags=v[12:20]
    # disk_aliashdr_t base 20 + 12 bytes per frame descriptor; C allocates sizeof(base)+nf*desc.
    header_size=20+12*nf; model_off=header_size; base_size=header_size+84+4*nv+8*nt
    out=bytearray(base_size); out[0:4]=struct.pack('<I',IDPM); struct.pack_into('<I',out,4,model_off)
    # Preserve disk_mdl_t fields exactly; C endian-copies them.
    out[model_off:model_off+84]=data[:84]
    st_off=model_off+84; tri_off=st_off+4*nv; struct.pack_into('<II',out,8,st_off,0); struct.pack_into('<I',out,16,tri_off)
    for i,(seam,s,t) in enumerate(st):
        if not (0<=s<32768 and -32768<=t<32768): raise ValueError('stvert outside packed range')
        word=(seam&1)|((s&0x7fff)<<1)|((t&0xffff)<<16); struct.pack_into('<I',out,st_off+4*i,word)
    # skin descriptors are 4-byte packed type:1/skin:31 in active minimized layout.
    skin_desc_off=len(out); out.extend(b'\0'*(4*numskins)); struct.pack_into('<I',out,12,skin_desc_off)
    skin_offsets=[]
    for si,skin in enumerate(skins):
        skin_start=len(out); skin_offsets.append(skin_start)
        for tri in tris:
            out.extend(skin_triangle_bytes(skin,sw,sh,coords_for(tri,st,sw)))
            while len(out)&3: out.append(0)
        struct.pack_into('<I',out,skin_desc_off+4*si,(skin_start<<1)&0xfffffffe)
    # triangle offset tables and packed mtriangle_t. Global skin stream offset matches C and is 4-aligned per triangle.
    stream_off=0
    for i,tri in enumerate(tris):
        coords=coords_for(tri,st,sw); vals,offs,buf,start,tsize,nextoff=offset_table(coords,stream_off); stream_off=nextoff
        while len(out)&3: out.append(0)
        tod=len(out); out.extend(struct.pack('<HHH',offs,buf,start)); out.extend(b''.join(struct.pack('<H',x) for x in vals))
        front,inds=tri
        if any(x>=512 for x in inds) or tsize>=8192 or len(vals)>=256 or (tod>>2)>=32768: raise ValueError('MG24 packed triangle field overflow')
        w0=(front&1)|((tsize&0x1fff)<<1)|((inds[0]&0x1ff)<<14)|((inds[1]&0x1ff)<<23)
        w1=(inds[2]&0x1ff)|(((tod>>2)&0x7fff)<<9)|((len(vals)&0xff)<<24)
        struct.pack_into('<II',out,tri_off+8*i,w0,w1)
    # frames: descriptors live in header; vertex arrays/groups appended after triangle metadata.
    frame_base=20
    for i,fr in enumerate(frames):
        if fr[0]=='single':
            _,bmin,bmax,name,verts=fr
            while len(out)&3: out.append(0)
            foff=len(out); out.extend(verts); packed=(foff<<1) # type 0 in low bit
            out[frame_base+12*i:frame_base+12*i+8]=bmin+bmax; struct.pack_into('<I',out,frame_base+12*i+8,packed)
        else:
            _,bmin,bmax,intervals,group=fr
            while len(out)&3: out.append(0)
            goff=len(out); ng=len(group)
            # maliasgroup_t: numframes:9 / intervals:23 followed by ng+1? C allocates sizeof + numframes*desc.
            group_hdr_pos=len(out); out.extend(b'\0'*(4+12*(ng+1)))
            intervals_off=len(out); out.extend(struct.pack('<'+'f'*ng,*intervals))
            firstword=(ng&0x1ff)|((intervals_off&0x7fffff)<<9); struct.pack_into('<I',out,group_hdr_pos,firstword)
            for j,(gbmin,gbmax,name,verts) in enumerate(group):
                while len(out)&3: out.append(0)
                voff=len(out); out.extend(verts); pos=group_hdr_pos+4+12*j; out[pos:pos+8]=gbmin+gbmax; struct.pack_into('<I',out,pos+8,voff)
            out[frame_base+12*i:frame_base+12*i+8]=bmin+bmax; struct.pack_into('<I',out,frame_base+12*i+8,(goff<<1)|1)
    meta={'skinwidth':sw,'skinheight':sh,'skins':numskins,'vertices':nv,'triangles':nt,'frames':nf,
          'input_bytes':len(data),'output_bytes':len(out),'skin_stream_bytes_per_skin':stream_off}
    return bytes(out),meta
