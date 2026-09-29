"""Emit immutable ARM alias/sprite metadata for the native resource compiler."""
import struct

def emit_alias(c, name, data, raw_offset):
    u=lambda off:struct.unpack_from('<I',data,off)[0]
    model,stverts,skins,triangles=struct.unpack_from('<4I',data,4)
    p=struct.unpack_from('<2i3f3ff3f8if',data,model)
    ns,w,h,nv,nt,nf,sync,flags=p[12:20]
    if not 0<nf<256 or not 0<ns<256:raise ValueError('native alias scalar capacity')
    frames=bytearray(data[20:20+12*nf]);groups=bytearray()
    groupbase=20+12*nf+56+4*nv+12*ns
    for i in range(nf):
        bits=struct.unpack_from('<I',frames,12*i+8)[0]
        if bits&1:
            off=bits>>1;word=u(off);count=word&511;intervals=word>>9
            if not 0<count<256:raise ValueError('alias group capacity')
            at=groupbase+len(groups);interval_at=at+4+count*12
            struct.pack_into('<I',frames,12*i+8,(at<<1)|1)
            groups.extend(struct.pack('<I',count|(interval_at<<9)))
            groups.extend(data[off+4:off+4+count*12]);groups.extend(data[intervals:intervals+4*count])
    def b(x):return '{'+','.join(map(str,x))+'}'
    def f(x):return '{'+','.join(float(v).hex()+'f' for v in x)+'}'
    skinrows=[]
    for i in range(ns):
        bits,original=struct.unpack_from('<II',data,skins+8*i)
        if bits&1:raise ValueError('MG24 alias skin groups unsupported')
        skinrows.append('{'+f'.skin={bits>>1},.originalSkin={original},.pCachedSkin=(byte*)0xffffffffu'+'}')
    c.append(f'typedef struct {{ uintptr_t extMemAddress; int model,stverts,skindesc,triangles; byte frames[{nf*12}]; mdl_t mdl; byte st[{nv*4}]; maliasskindesc_t skins[{ns}]; byte groups[{len(groups)}]; }} {name}_t;')
    c.append(f'_Static_assert(sizeof(mdl_t)==56 && sizeof(aliashdr_t)==20 && sizeof(maliasskindesc_t)==12 && offsetof({name}_t,groups)=={groupbase},"alias ABI changed");')
    c.append(f'const {name}_t {name}={{.extMemAddress=(uintptr_t)(raw+{raw_offset}),.model=offsetof({name}_t,mdl),.stverts=offsetof({name}_t,st),.skindesc=offsetof({name}_t,skins),.triangles={triangles},.frames={b(frames)},.mdl={{'+f'.scale={f(p[2:5])},.scale_origin={f(p[5:8])},.eyeposition={f(p[9:12])},.boundingradius={float(p[8]).hex()}f,.size={float(p[20]).hex()}f,.skinwidth={w},.skinheight={h},.numverts={nv},.numtris={nt},.numskins={ns},.synctype={sync},.numframes={nf},.flags={flags}'+'},.st='+b(data[stverts:stverts+nv*4])+',.skins={'+','.join(skinrows)+'},.groups='+b(groups)+'};')
    c.append(f'const model_t {name}_model={{.type=mod_alias,.numframes={nf},.synctype={sync},.flags={flags},.mins_s={{-16,-16,-16}},.maxs_s={{16,16,16}},.data=(void*)&{name}}};')
    c.append(f'const qrn_brush_t {name}_entry={{QRN_ALIAS_MAGIC,1,&{name}_model}};')
    return f'(const byte*)&{name}_entry',f'sizeof({name}_entry)',2

def emit_sprite(c,name,data,raw_offset):
    ident,version,typ,radius,w,h,nf,beam,sync=struct.unpack_from('<iiifiiifi',data)
    if version!=1 or not 0<nf<256:raise ValueError('invalid sprite')
    off=36;rows=[]
    for i in range(nf):
        kind=struct.unpack_from('<i',data,off)[0];off+=4
        if kind:raise ValueError('MG24 sprite groups unsupported')
        x,y,fw,fh=struct.unpack_from('<4i',data,off);off+=16
        rows.append('{'+f'.width={fw},.height={fh},.up={y},.down={y-fh},.left={x},.right={x+fw},.pixels=(byte*)(raw+{raw_offset+off})'+'}')
        off+=fw*fh
    c.append(f'const mspriteframe_t {name}_frames[{nf}]={{'+','.join(rows)+'};')
    c.append(f'const struct {{byte type,numframes;short maxwidth,maxheight,beamlength;mspriteframedesc_t frames[{nf}];}} {name}={{'+f'{typ},{nf},{w},{h},{int(beam)},'+'{'+','.join('{SPR_SINGLE,(mspriteframe_t*)&'+name+'_frames['+str(i)+']}' for i in range(nf))+'}};')
    c.append(f'const model_t {name}_model={{.type=mod_sprite,.numframes={nf},.synctype={sync},.mins_s={{-{w//2},-{w//2},-{h//2}}},.maxs_s={{{w//2},{w//2},{h//2}}},.data=(void*)&{name}}};')
    c.append(f'const qrn_brush_t {name}_entry={{QRN_SPRITE_MAGIC,1,&{name}_model}};')
    return f'(const byte*)&{name}_entry',f'sizeof({name}_entry)',3
