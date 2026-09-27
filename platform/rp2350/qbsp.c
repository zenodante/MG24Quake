/* BSP29 layout follows QuakeMG24/Quake/bspfile.h's original BSP records.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "qbsp.h"
#include <math.h>
#include <string.h>

static const uint8_t stride[QBSP_LUMPS]={0,20,0,12,0,24,40,20,0,8,28,2,4,4,64};
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static int32_t i32(const uint8_t *p) { return (int32_t)u32(p); }
static unsigned u16(const uint8_t *p) { return p[0]|(unsigned)p[1]<<8; }
static int i16(const uint8_t *p) { return (int16_t)u16(p); }
static float f32(const uint8_t *p) {
    uint32_t bits=u32(p); float value; memcpy(&value,&bits,4); return value;
}
static bool range(uint32_t start, size_t size, uint32_t limit) {
    return start<=limit && size<=limit-start;
}
static bool contents_ok(int32_t n) { return n>=-14 && n<=-1; }
const uint8_t *qbsp_record(const qbsp_t *b, unsigned lump, uint32_t index) {
    if (!b || !b->pak || lump>=QBSP_LUMPS || !stride[lump] ||
        !b->lump[lump].mapped || index>=b->lump[lump].count) return NULL;
    return b->lump[lump].mapped+(size_t)index*stride[lump];
}
bool qbsp_read(const qbsp_t *b,unsigned lump,uint32_t offset,void *out,size_t size,qpak_cache_t *cache) {
    return b && b->pak && lump<QBSP_LUMPS && range(offset,size,b->lump[lump].size) &&
        qpak_read(b->pak,&b->file,cache,b->lump[lump].offset+offset,out,size);
}
bool qbsp_texture_mip(const qbsp_t *b,uint32_t texture,unsigned mip,
                       uint32_t *offset,uint32_t *width,uint32_t *height,qpak_cache_t *cache) {
    uint8_t word[4],header[40];
    if (!b || !offset || !width || !height || texture>=b->textures || mip>3 ||
        !qbsp_read(b,QBSP_TEXTURES,4+texture*4,word,4,cache)) return false;
    uint32_t start=u32(word);
    if (start<4+b->textures*4 || !qbsp_read(b,QBSP_TEXTURES,start,header,40,cache)) return false;
    uint32_t w=u32(header+16),h=u32(header+20),relative=u32(header+24+mip*4);
    if (!w || !h || (w&15) || (h&15) || relative<40) return false;
    w>>=mip; h>>=mip;
    if (!range(start,relative,b->lump[QBSP_TEXTURES].size) ||
        (uint64_t)w*h>b->lump[QBSP_TEXTURES].size-start-relative) return false;
    *offset=start+relative; *width=w; *height=h;
    return true;
}
static bool node_ref(const qbsp_t *b,int32_t n,bool clip) {
    if (n>=0) return (uint32_t)n<b->lump[clip?QBSP_CLIPNODES:QBSP_NODES].count;
    return clip?contents_ok(n):(uint32_t)(-(int64_t)n-1)<b->lump[QBSP_LEAVES].count;
}
static bool validate(qbsp_t *b,qpak_cache_t *cache) {
    const uint8_t *p;
    if (!b->lump[QBSP_MODELS].count || !b->lump[QBSP_LEAVES].count ||
        !b->lump[QBSP_PLANES].count) return false;
    uint8_t word[4];
    if (!qbsp_read(b,QBSP_TEXTURES,0,word,4,cache)) return false;
    b->textures=u32(word);
    if (b->textures>(b->lump[QBSP_TEXTURES].size-4)/4) return false;
    for (uint32_t i=0;i<b->textures;++i) {
        if (!qbsp_read(b,QBSP_TEXTURES,4+i*4,word,4,cache)) return false;
        if (i32(word)==-1) continue;
        for (unsigned m=0;m<4;++m) {
            uint32_t off,w,h;
            if (!qbsp_texture_mip(b,i,m,&off,&w,&h,cache)) return false;
        }
    }
    for (unsigned l=0;l<QBSP_LUMPS;++l) for (uint32_t i=0;i<b->lump[l].count && stride[l];++i) {
        p=qbsp_record(b,l,i);
        switch (l) {
        case QBSP_PLANES:
            for(unsigned k=0;k<4;++k) if(!isfinite(f32(p+4*k))) return false;
            if(u32(p+16)>5) return false;
            break;
        case QBSP_VERTICES:
            for(unsigned k=0;k<3;++k) if(!isfinite(f32(p+4*k))) return false;
            break;
        case QBSP_EDGES:
            if(u16(p)>=b->lump[QBSP_VERTICES].count || u16(p+2)>=b->lump[QBSP_VERTICES].count) return false;
            break;
        case QBSP_SURFEDGES: {
            int64_t edge=i32(p); if(edge<0) edge=-edge;
            if((uint64_t)edge>=b->lump[QBSP_EDGES].count) return false;
            break;
        }
        case QBSP_TEXINFO:
            for(unsigned k=0;k<8;++k) if(!isfinite(f32(p+4*k))) return false;
            if(u32(p+32)>=b->textures) return false;
            break;
        case QBSP_FACES:
            if(u16(p)>=b->lump[QBSP_PLANES].count || u16(p+2)>1 ||
               !range(u32(p+4),u16(p+8),b->lump[QBSP_SURFEDGES].count) ||
               u16(p+10)>=b->lump[QBSP_TEXINFO].count ||
               (i32(p+16)!=-1 && u32(p+16)>=b->lump[QBSP_LIGHTING].size)) return false;
            break;
        case QBSP_MARKSURFACES:
            if(u16(p)>=b->lump[QBSP_FACES].count) return false;
            break;
        case QBSP_NODES: case QBSP_CLIPNODES:
            if(u32(p)>=b->lump[QBSP_PLANES].count ||
               !node_ref(b,i16(p+4),l==QBSP_CLIPNODES) || !node_ref(b,i16(p+6),l==QBSP_CLIPNODES)) return false;
            if(l==QBSP_NODES && !range(u16(p+20),u16(p+22),b->lump[QBSP_FACES].count)) return false;
            break;
        case QBSP_LEAVES:
            if(!contents_ok(i32(p)) || (i!=0 && i32(p+4)!=-1 && u32(p+4)>=b->lump[QBSP_VISIBILITY].size) ||
               !range(u16(p+20),u16(p+22),b->lump[QBSP_MARKSURFACES].count)) return false;
            break;
        case QBSP_MODELS:
            for(unsigned k=0;k<9;++k) if(!isfinite(f32(p+4*k))) return false;
            for(unsigned k=0;k<4;++k) if(!node_ref(b,i32(p+36+4*k),k!=0)) return false;
            if(u32(p+52)>=b->lump[QBSP_LEAVES].count ||
               !range(u32(p+56),u32(p+60),b->lump[QBSP_FACES].count)) return false;
            break;
        default: break;
        }
    }
    b->visleaves=u32(qbsp_record(b,QBSP_MODELS,0)+52);
    return i32(qbsp_record(b,QBSP_LEAVES,0))==-2;
}
bool qbsp_open(qbsp_t *out,const qpak_t *pak,const char *name,qpak_cache_t *cache) {
    if(!out) return false;
    memset(out,0,sizeof *out);
    qbsp_t b={.pak=pak}; uint8_t header[124];
    if(!qpak_find(pak,name,&b.file) || !qpak_read(pak,&b.file,cache,0,header,sizeof header) || u32(header)!=29) return false;
    for(unsigned l=0;l<QBSP_LUMPS;++l) {
        qbsp_lump_t *v=&b.lump[l];
        v->offset=u32(header+4+l*8); v->size=u32(header+8+l*8);
        if(!range(v->offset,v->size,b.file.size) || (v->size && v->offset<sizeof header) ||
           (stride[l] && v->size%stride[l])) return false;
        v->count=stride[l]?v->size/stride[l]:0;
        v->mapped=qpak_map(pak,&b.file,v->offset,v->size);
        // Packing guarantees geometry/entities/PVS are directly accessible.
        if(v->size && l!=QBSP_TEXTURES && l!=QBSP_LIGHTING && !v->mapped) return false;
        for(unsigned j=0;j<l;++j) {
            const qbsp_lump_t *a=&b.lump[j];
            if(v->size && a->size && v->offset<a->offset+a->size && a->offset<v->offset+v->size) return false;
        }
    }
    if(!validate(&b,cache)) return false;
    *out=b; return true;
}
static bool locate(const qbsp_t *b,uint32_t model,unsigned hull,const float point[3],int32_t *result) {
    if(!b || !point || !result || hull>3) return false;
    for(unsigned i=0;i<3;++i) if(!isfinite(point[i])) return false;
    const uint8_t *m=qbsp_record(b,QBSP_MODELS,model);
    if(!m) return false;
    int32_t n=i32(m+36+4*hull);
    unsigned lump=hull?QBSP_CLIPNODES:QBSP_NODES;
    uint32_t remaining=b->lump[lump].count;
    while(n>=0) {
        if(!remaining--) return false; // reject cycles without recursion/stack growth
        const uint8_t *node=qbsp_record(b,lump,(uint32_t)n);
        if(!node) return false;
        const uint8_t *plane=qbsp_record(b,QBSP_PLANES,u32(node));
        if(!plane) return false;
        unsigned type=u32(plane+16);
        float d=type<3?point[type]-f32(plane+12):
            point[0]*f32(plane)+point[1]*f32(plane+4)+point[2]*f32(plane+8)-f32(plane+12);
        n=i16(node+4+(d<0?2:0));
    }
    if(!node_ref(b,n,hull!=0)) return false;
    *result=n; return true;
}
bool qbsp_point_leaf(const qbsp_t *b,uint32_t model,const float point[3],uint32_t *leaf) {
    int32_t n;
    if(!leaf || !locate(b,model,0,point,&n)) return false;
    *leaf=(uint32_t)(-(int64_t)n-1); return true;
}
bool qbsp_point_contents(const qbsp_t *b,uint32_t model,unsigned hull,const float point[3],int32_t *contents) {
    int32_t n;
    if(!contents || !locate(b,model,hull,point,&n)) return false;
    *contents=hull?n:i32(qbsp_record(b,QBSP_LEAVES,(uint32_t)(-(int64_t)n-1)));
    return true;
}
size_t qbsp_pvs_bytes(const qbsp_t *b) { return b && b->pak?((size_t)b->visleaves+7)/8:0; }
bool qbsp_leaf_pvs(const qbsp_t *b,uint32_t leaf,uint8_t *out,size_t capacity) {
    const uint8_t *p=qbsp_record(b,QBSP_LEAVES,leaf);
    size_t row=qbsp_pvs_bytes(b);
    if(!p || !out || capacity<row) return false;
    if(!leaf || i32(p+4)==-1) { memset(out,255,row); return true; }
    uint32_t pos=u32(p+4),limit=b->lump[QBSP_VISIBILITY].size;
    const uint8_t *vis=b->lump[QBSP_VISIBILITY].mapped;
    size_t done=0;
    while(done<row) {
        if(pos>=limit || !vis) return false;
        unsigned value=vis[pos++];
        if(value) { out[done++]=(uint8_t)value; continue; }
        if(pos>=limit) return false;
        unsigned run=vis[pos++];
        if(!run || run>row-done) return false;
        memset(out+done,0,run); done+=run;
    }
    return true;
}
