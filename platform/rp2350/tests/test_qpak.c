#include "qpak.h"
#include "qmix.h"
#include "qfiles.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint8_t *load(const char *path,size_t *size) {
    FILE *f=fopen(path,"rb"); assert(f);
    assert(!fseek(f,0,SEEK_END)); long n=ftell(f); assert(n>0); rewind(f);
    uint8_t *data=malloc((size_t)n); assert(data);
    assert(fread(data,1,(size_t)n,f)==(size_t)n); fclose(f); *size=(size_t)n; return data;
}
int main(int argc,char **argv) {
    assert(argc==3);
    size_t image_size,original_size;
    uint8_t *image=load(argv[1],&image_size),*original=load(argv[2],&original_size);
    qpak_t pak; qpak_cache_t cache={0};
    assert(qpak_open(&pak,image,image_size));
    assert(!qpak_open(&(qpak_t){0},image,image_size-1));
    uint32_t dir=rd32(original+4),count=rd32(original+8)/64;
    assert(pak.files==count);
    qfiles_mount(&pak);
    unsigned sounds=0,demos=0; size_t total=0;
    for (uint32_t i=0;i<count;++i) {
        const uint8_t *entry=original+dir+i*64;
        char name[57]; memcpy(name,entry,56); name[56]=0;
        uint32_t start=rd32(entry+56),length=rd32(entry+60);
        assert((size_t)start+length<=original_size);
        qpak_file_t file; assert(qpak_find(&pak,name,&file)); assert(file.size==length);
        uint8_t *out=malloc(length?length:1); assert(out);
        assert(qpak_read(&pak,&file,&cache,0,out,length));
        assert(!memcmp(out,original+start,length));
        assert(qpak_crc32(out,length)==file.crc32);
        // Exercise Quake's handle API against the original PAK, including
        // independent cursors and eviction of the shared decompression cache.
        int a,b;
        assert(Sys_FileOpenRead(name,&a)==(int)length);
        assert(Sys_FileOpenRead(name,&b)==(int)length && a!=b);
        assert(Sys_FileTime(name)==1);
        uint8_t part[509];
        for (uint32_t pos=0;pos<length;pos+=sizeof part) {
            size_t n=length-pos; if(n>sizeof part)n=sizeof part;
            assert(Sys_FileRead(a,part,sizeof part)==(int)n);
            assert(!memcmp(part,original+start+pos,n));
            uint32_t back=length-pos-(uint32_t)n;
            Sys_FileSeek(b,(int)back);
            assert(Sys_FileRead(b,part,(int)n)==(int)n);
            assert(!memcmp(part,original+start+back,n));
        }
        assert(Sys_FileRead(a,part,1)==0);
        Sys_FileSeek(a,-1); // invalid seeks preserve EOF
        Sys_FileSeek(a,(int)length+1);
        assert(Sys_FileRead(a,part,1)==0);
        Sys_FileSeek(a,0);
        assert(Sys_FileRead(a,NULL,1)==0);
        assert(Sys_FileRead(a,part,-1)==0);
        if(length) {
            assert(Sys_FileRead(a,part,1)==1);
            assert(part[0]==original[start]);
        }
        const uint8_t *mapped_file=qfiles_map(a,0,length);
        assert(mapped_file==qpak_map(&pak,&file,0,length));
        Sys_FileClose(a); Sys_FileClose(b);
        assert(Sys_FileRead(a,part,1)==0);
        assert(!qfiles_map(a,0,1));
        assert(!qpak_read(&pak,&file,&cache,length,out,1));
        assert(!qpak_read(&pak,&file,&cache,UINT32_MAX,out,1));
        // Boundary-straddling reads and cache evictions, not just sequential access.
        for (uint32_t pos=0;pos<length;pos+=4093) {
            size_t n=length-pos; if(n>19)n=19;
            uint8_t small[19];
            assert(qpak_read(&pak,&file,&cache,pos,small,n));
            assert(!memcmp(small,original+start+pos,n));
        }
        if (!strncmp(name,"sound/",6)) {
            const uint8_t *mapped=qpak_map(&pak,&file,0,length);
            assert(mapped && !memcmp(mapped,out,length));
            qsound_t sound; assert(qsound_wav(&sound,mapped,length));
            qmix_t mixer={0}; uint16_t audio[256];
            qmix_start(&mixer,0,&sound,255,255); qmix_render(&mixer,audio,256);
            for(unsigned j=0;j<256;++j)assert(audio[j]<=255);
            ++sounds;
        }
        if (strstr(name,".dem")) ++demos;
        if (strstr(name,".bsp")) {
            for(unsigned lump=0;lump<15;++lump) {
                if(lump==2 || lump==8)continue;
                uint32_t o=rd32(out+4+8*lump),n=rd32(out+8+8*lump);
                if (!n)continue;
                const uint8_t *mapped=qpak_map(&pak,&file,o,n);
                assert(mapped && !memcmp(mapped,out+o,n));
            }
        }
        total+=length; free(out);
    }
    int opened[QFILES_MAX_HANDLES],missing;
    for(unsigned i=0;i<QFILES_MAX_HANDLES;++i)
        assert(Sys_FileOpenRead("gfx/palette.lmp",&opened[i])==768);
    assert(Sys_FileOpenRead("gfx/palette.lmp",&missing)==-1 && missing==-1);
    Sys_FileClose(opened[0]);
    assert(Sys_FileOpenRead("gfx/palette.lmp",&missing)==768);
    assert(Sys_FileOpenRead("missing.file",&missing)==-1 && missing==-1);
    assert(Sys_FileOpenRead(NULL,&missing)==-1);
    assert(Sys_FileOpenRead("gfx/palette.lmp",NULL)==-1);
    assert(Sys_FileTime("missing.file")==-1);
    assert(Sys_FileOpenWrite("config.cfg")==-1);
    assert(Sys_FileWrite(opened[1],image,1)==0);
    assert(Sys_FileRead(-1,image,1)==0);
    assert(Sys_FileRead(QFILES_MAX_HANDLES+1,image,1)==0);
    qfiles_mount(NULL);
    assert(Sys_FileRead(opened[1],image,1)==0);
    assert(Sys_FileOpenRead("gfx/palette.lmp",&missing)==-1);
    qfiles_mount(&pak);
    assert(Sys_FileOpenRead("gfx/palette.lmp",&missing)==768);
    Sys_FileClose(missing);
    // Reject metadata damage and truncated/invalid LZ4 streams.
    image[64]^=1; assert(!qpak_open(&(qpak_t){0},image,image_size)); image[64]^=1;
    uint8_t dst[4096];
    const uint8_t bad_offset[]={0,0,0};
    const uint8_t bad_literal[]={0xf0,255};
    assert(!qpak_lz4_decode(bad_offset,sizeof bad_offset,dst,4));
    assert(!qpak_lz4_decode(bad_literal,sizeof bad_literal,dst,4));
    // Arbitrary malformed blocks must terminate without touching outside dst.
    uint32_t rng=1;
    for(unsigned i=0;i<20000;++i) {
        uint8_t noise[64];
        for(unsigned j=0;j<sizeof noise;++j){rng=rng*1664525u+1013904223u;noise[j]=rng>>24;}
        (void)qpak_lz4_decode(noise,1+i%64,dst,1+i%4096);
    }
    assert(demos==3 && sounds>0);
    printf("PASS: %u files / %zu bytes identical, %u demos, %u XIP sounds; BSP geometry mapped; malformed inputs rejected\n",
           count,total,demos,sounds);
    free(image); free(original); return 0;
}
