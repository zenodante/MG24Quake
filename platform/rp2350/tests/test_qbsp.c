#include "qbsp.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static int32_t i32(const uint8_t *p) { return (int32_t)u32(p); }
static int i16(const uint8_t *p) { return (int16_t)(p[0]|(unsigned)p[1]<<8); }
static float f32(const uint8_t *p) { uint32_t x=u32(p); float f;memcpy(&f,&x,4);return f; }
static uint8_t *load(const char *path,size_t *size) {
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long n=ftell(f);assert(n>0);rewind(f);uint8_t *data=malloc((size_t)n);assert(data);
    assert(fread(data,1,(size_t)n,f)==(size_t)n);fclose(f);*size=(size_t)n;return data;
}
static const uint8_t *lump(const uint8_t *map,unsigned l) { return map+u32(map+4+l*8); }
/* Independent traversal of original PAK bytes, without QPAK or qbsp views. */
static int32_t reference(const uint8_t *map,unsigned model,unsigned hull,const float point[3]) {
    const uint8_t *m=lump(map,QBSP_MODELS)+64*model;
    int32_t n=i32(m+36+hull*4);
    while(n>=0) {
        const uint8_t *node=lump(map,hull?QBSP_CLIPNODES:QBSP_NODES)+n*(hull?8:24);
        const uint8_t *p=lump(map,QBSP_PLANES)+u32(node)*20;
        float d=0;
        // Same arithmetic order as Quake for exact boundary comparisons.
        if(u32(p+16)<3) d=point[u32(p+16)]-f32(p+12);
        else d=point[0]*f32(p)+point[1]*f32(p+4)+point[2]*f32(p+8)-f32(p+12);
        n=i16(node+4+(d<0?2:0));
    }
    return hull?n:i32(lump(map,QBSP_LEAVES)+(-n-1)*28);
}
int main(int argc,char **argv) {
    assert(argc==3);size_t image_size,original_size;
    uint8_t *image=load(argv[1],&image_size),*original=load(argv[2],&original_size);
    qpak_t pak;assert(qpak_open(&pak,image,image_size));qpak_cache_t cache={0};
    unsigned maps=0,queries=0;uint32_t rng=13;
    uint32_t directory=u32(original+4),files=u32(original+8)/64;
    for(uint32_t i=0;i<files;++i) {
        const uint8_t *entry=original+directory+i*64;
        char name[57];memcpy(name,entry,56);name[56]=0;
        if(!strstr(name,".bsp"))continue;
        assert((size_t)u32(entry+56)+u32(entry+60)<=original_size);
        const uint8_t *map=original+u32(entry+56);
        qbsp_t b;
        if(!qbsp_open(&b,&pak,name,&cache)) { fprintf(stderr,"BSP open failed: %s\n",name);return 1; }
        for(unsigned l=0;l<QBSP_LUMPS;++l) {
            size_t size=u32(map+8+l*8);assert(b.lump[l].size==size);
            uint8_t *data=malloc(size?size:1);assert(data);
            assert(qbsp_read(&b,l,0,data,size,&cache));
            assert(!memcmp(data,lump(map,l),size));
            assert(!qbsp_read(&b,l,(uint32_t)size,data,1,&cache));
            if(l!=QBSP_TEXTURES && l!=QBSP_LIGHTING && size)
                assert(b.lump[l].mapped && !memcmp(b.lump[l].mapped,data,size));
            free(data);
        }
        for(uint32_t texture=0;texture<b.textures;++texture) {
            const uint8_t *textures=lump(map,QBSP_TEXTURES);
            int32_t base=i32(textures+4+texture*4);
            for(unsigned mip=0;mip<4;++mip) {
                uint32_t offset,w,h;
                bool ok=qbsp_texture_mip(&b,texture,mip,&offset,&w,&h,&cache);
                if(base==-1) { assert(!ok);continue; }
                assert(ok);
                assert(w==(u32(textures+base+16)>>mip));
                assert(h==(u32(textures+base+20)>>mip));
                assert(offset==(uint32_t)base+u32(textures+base+24+mip*4));
                uint8_t pixels[257];
                for(uint32_t pos=0;pos<w*h;pos+=sizeof pixels) {
                    size_t n=w*h-pos;if(n>sizeof pixels)n=sizeof pixels;
                    assert(qbsp_read(&b,QBSP_TEXTURES,offset+pos,pixels,n,&cache));
                    assert(!memcmp(pixels,textures+offset+pos,n));
                }
            }
        }
        uint8_t pvs[8192];size_t row=qbsp_pvs_bytes(&b);assert(row<=sizeof pvs);
        for(uint32_t leaf=0;leaf<b.lump[QBSP_LEAVES].count;++leaf) {
            assert(qbsp_leaf_pvs(&b,leaf,pvs,sizeof pvs));
            const uint8_t *rawleaf=lump(map,QBSP_LEAVES)+leaf*28;
            int32_t offset=i32(rawleaf+4);
            if(!leaf || offset==-1) { for(size_t j=0;j<row;++j)assert(pvs[j]==255); }
            else {
                const uint8_t *src=lump(map,QBSP_VISIBILITY)+offset;
                size_t n=0;
                while(n<row) {
                    if(*src) assert(pvs[n++]==*src++);
                    else { ++src;unsigned run=*src++;assert(run && run<=row-n);while(run--)assert(pvs[n++]==0); }
                }
            }
        }
        assert(!qbsp_leaf_pvs(&b,b.lump[QBSP_LEAVES].count,pvs,sizeof pvs));
        if(row) assert(!qbsp_leaf_pvs(&b,0,pvs,row-1));
        assert(!qbsp_record(&b,QBSP_PLANES,b.lump[QBSP_PLANES].count));
        for(uint32_t model=0;model<b.lump[QBSP_MODELS].count;++model) {
            const uint8_t *m=lump(map,QBSP_MODELS)+64*model;
            for(unsigned q=0;q<100;++q) {
                float point[3];
                for(unsigned k=0;k<3;++k) {
                    rng=rng*1664525u+1013904223u;
                    float t=(float)(rng&65535)/65535.0f;
                    point[k]=f32(m+4*k)-64+t*(f32(m+12+4*k)-f32(m+4*k)+128);
                }
                for(unsigned hull=0;hull<4;++hull) {
                    int32_t got;assert(qbsp_point_contents(&b,model,hull,point,&got));
                    assert(got==reference(map,model,hull,point));++queries;
                }
                uint32_t leaf;assert(qbsp_point_leaf(&b,model,point,&leaf));
                assert(i32(lump(map,QBSP_LEAVES)+leaf*28)==reference(map,model,0,point));
            }
        }
        const float invalid[3]={NAN,0,0};uint32_t leaf;
        assert(!qbsp_point_leaf(&b,0,invalid,&leaf));
        // Mutate mapped BSP bytes only; QPAK metadata remains intact.
        uint8_t *header=(uint8_t *)qpak_map(&pak,&b.file,0,124);assert(header);
        uint8_t saved=header[0];header[0]=30;qbsp_t rejected;
        assert(!qbsp_open(&rejected,&pak,name,&cache));assert(!rejected.pak);header[0]=saved;
        uint8_t length[4];memcpy(length,header+8+QBSP_PLANES*8,4);
        header[8+QBSP_PLANES*8]^=1; // partial fixed-size record
        assert(!qbsp_open(&rejected,&pak,name,&cache));
        memcpy(header+8+QBSP_PLANES*8,length,4);
        uint8_t offset_copy[4];memcpy(offset_copy,header+4+QBSP_VERTICES*8,4);
        memset(header+4+QBSP_VERTICES*8,255,4); // out-of-file lump
        assert(!qbsp_open(&rejected,&pak,name,&cache));
        memcpy(header+4+QBSP_VERTICES*8,header+4+QBSP_PLANES*8,4); // overlapping lumps
        assert(!qbsp_open(&rejected,&pak,name,&cache));
        memcpy(header+4+QBSP_VERTICES*8,offset_copy,4);
        uint8_t *surfedge=(uint8_t *)b.lump[QBSP_SURFEDGES].mapped;
        if(surfedge) {
            uint8_t edge_copy[4];memcpy(edge_copy,surfedge,4);
            memset(surfedge,0,4);surfedge[3]=128; // INT_MIN must not overflow abs()
            assert(!qbsp_open(&rejected,&pak,name,&cache));
            memcpy(surfedge,edge_copy,4);
        }
        uint8_t *node=(uint8_t *)b.lump[QBSP_NODES].mapped;
        if(node) {
            uint8_t copy[24];memcpy(copy,node,24);
            memset(node,255,4);assert(!qbsp_open(&rejected,&pak,name,&cache));
            memcpy(node,copy,24);
            // Both children point to self: traversal must terminate with failure.
            memset(node+4,0,4);
            uint8_t *model=(uint8_t *)b.lump[QBSP_MODELS].mapped;
            uint8_t root[4];memcpy(root,model+36,4);memset(model+36,0,4);
            const float origin[3]={0,0,0};assert(!qbsp_point_leaf(&b,0,origin,&leaf));
            memcpy(model+36,root,4);memcpy(node,copy,24);
        }
        // Zero-length and oversized visibility runs must not loop or overwrite.
        if(row && b.lump[QBSP_VISIBILITY].size>=2 && b.lump[QBSP_LEAVES].count>1) {
            uint8_t *vis=(uint8_t *)b.lump[QBSP_VISIBILITY].mapped;
            uint8_t *vleaf=(uint8_t *)b.lump[QBSP_LEAVES].mapped+28;
            uint8_t copy[4],v0=vis[0],v1=vis[1];memcpy(copy,vleaf+4,4);memset(vleaf+4,0,4);
            vis[0]=vis[1]=0;assert(!qbsp_leaf_pvs(&b,1,pvs,sizeof pvs));
            vis[0]=0;vis[1]=255;
            if(row<255)assert(!qbsp_leaf_pvs(&b,1,pvs,sizeof pvs));
            vis[0]=v0;vis[1]=v1;memcpy(vleaf+4,copy,4);
        }
        printf("PASS BSP %s: %u models, %u faces, %u leaves, %zu PVS bytes\n",name,
            b.lump[QBSP_MODELS].count,b.lump[QBSP_FACES].count,b.lump[QBSP_LEAVES].count,row);
        ++maps;
    }
    assert(maps>=9);printf("PASS: %u original BSP maps, %u hull queries, all leaf PVS rows, malformed data checks\n",maps,queries);
    free(image);free(original);return 0;
}
