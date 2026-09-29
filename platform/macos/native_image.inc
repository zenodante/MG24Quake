/* QNAT1 is a generated host-ABI image, paired with one exact QXIP by CRC.
 * Header 64 bytes; entries 64 bytes: name[48], model offset, model bytes,
 * relocation offset, count. Each relocation is {slot u32,target u32}; target
 * bit 31 selects QXIP, otherwise it selects this image. Slots are native
 * pointers, zero on disk. All offsets are bytes; no SRAM pointer compression.
 * Only the packer uses compile_level/calloc. The player never reconstructs
 * planes, nodes, surfaces or texinfo. */
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
static byte *native_image;
static size_t native_bytes,native_relocs;
static const qpak_t *native_pak;
static void put32(byte *p,uint32_t v){memcpy(p,&v,4);}
static size_t align8(size_t n){return (n+7)&~(size_t)7;}
#include "native_abi.h"
size_t mg24_image_bytes(void){return native_bytes;}
size_t mg24_relocations(void){return native_relocs;}
void mg24_shutdown(void){if(native_image)munmap(native_image,native_bytes);native_image=NULL;native_bytes=0;native_relocs=0;native_pak=NULL;bound=NULL;mod_known=NULL;}

/* Offline serialization: emit only declared pointer fields, never scan words
 * that might merely resemble addresses. */
typedef struct {byte *out;size_t cap,reloc;uint32_t count,base,data;const qpak_t *pak;} native_writer_t;
static bool emit_pointer(native_writer_t *wr,uint32_t slot,const void *value){
    memset(wr->out+slot,0,sizeof(void*));
    if(!value)return true;
    uintptr_t v=(uintptr_t)value,b=(uintptr_t)binding,q=(uintptr_t)wr->pak->image;
    uint32_t target;
    if(v>=b && v-b<binding_size)target=wr->data+(uint32_t)(v-b);
    else if(v== (uintptr_t)&compile_brush)target=wr->base+(uint32_t)align8(sizeof(model_t));
    else if(v>=q && v-q<wr->pak->bytes)target=0x80000000u+(uint32_t)(v-q);
    else return false;
    if(wr->reloc+8>wr->cap)return false;
    put32(wr->out+wr->reloc,slot);put32(wr->out+wr->reloc+4,target);wr->reloc+=8;wr->count++;
    return true;
}
#define EMIT_AT(offset,value) do{if(!emit_pointer(&wr,(uint32_t)(offset),(value)))goto fail;}while(0)
#define EMIT_FIELD(ptr,field) EMIT_AT(wr.data+(byte*)&(ptr)->field-(byte*)binding,(ptr)->field)
bool mg24_pack(const qpak_t *pak,const char *output){
    if(sizeof(void*)!=8 || pak->xip_version!=3)return false;
    uint32_t count=0;
    for(unsigned i=0;i<pak->files;i++)if(u32(pak->image+pak->directory+16*i+4)==1)count++;
    /* Capacity is a packer-only workspace; never part of the game budget. */
    size_t cap=64u*1024*1024;byte *out=calloc(1,cap);if(!out)return false;
    size_t pos=64+64*count;uint32_t entry=0;
    for(unsigned i=0;i<pak->files;i++){
        const byte *dir=pak->image+pak->directory+16*i;
        if(u32(dir+4)!=1)continue;
        const char *name=(const char*)pak->image+pak->strings+u32(dir);
        if(strlen(name)>=48)goto fail;
        qbsp_t world;if(!qbsp_open(&world,pak,name,NULL) || !compile_level(&world))goto fail;
        size_t model_size=align8(sizeof(model_t)),brush_size=align8(sizeof(brush_model_data_t));
        size_t body_size=align8(model_size+brush_size+binding_size);
        if(pos+body_size>cap || pos+body_size>=0x80000000u)goto fail;
        native_writer_t wr={out,cap,pos+body_size,0,(uint32_t)pos,(uint32_t)(pos+model_size+brush_size),pak};
        byte *e=out+64+64*entry++;strcpy((char*)e,name);put32(e+48,pos);put32(e+52,body_size);put32(e+56,wr.reloc);
        memcpy(out+pos,&compile_model,sizeof compile_model);memcpy(out+pos+model_size,&compile_brush,sizeof compile_brush);memcpy(out+wr.data,binding,binding_size);
        EMIT_AT(pos+offsetof(model_t,brushModelData),&compile_brush);
#define EMIT_BRUSH(field) EMIT_AT(pos+model_size+offsetof(brush_model_data_t,field),compile_brush.field)
        EMIT_BRUSH(planes);EMIT_BRUSH(leafs);EMIT_BRUSH(vertexes);EMIT_BRUSH(edges);EMIT_BRUSH(nodes);
        EMIT_BRUSH(texinfo);EMIT_BRUSH(surfaces);EMIT_BRUSH(surfedges);EMIT_BRUSH(marksurfaceIdx);
        EMIT_BRUSH(textures);EMIT_BRUSH(visdata);EMIT_BRUSH(lightdata);EMIT_BRUSH(entities);
        for(unsigned h=0;h<MAX_MAP_HULLS;h++){EMIT_BRUSH(hulls[h].clipnodes);EMIT_BRUSH(hulls[h].planes);}
        for(unsigned j=1;j<compile_models;j++){
            model_t *m=compile_inline+j-1;brush_model_data_t *b=compile_inline_brush+j-1;
            EMIT_FIELD(m,brushModelData);
            EMIT_FIELD(b,planes);EMIT_FIELD(b,leafs);EMIT_FIELD(b,vertexes);EMIT_FIELD(b,edges);EMIT_FIELD(b,nodes);
            EMIT_FIELD(b,texinfo);EMIT_FIELD(b,surfaces);EMIT_FIELD(b,surfedges);EMIT_FIELD(b,marksurfaceIdx);
            EMIT_FIELD(b,textures);EMIT_FIELD(b,visdata);EMIT_FIELD(b,lightdata);EMIT_FIELD(b,entities);
            for(unsigned h=0;h<MAX_MAP_HULLS;h++){EMIT_FIELD(b,hulls[h].clipnodes);EMIT_FIELD(b,hulls[h].planes);}
        }
#undef EMIT_BRUSH
        for(unsigned j=0;j<world.textures;j++){
            texture_t *t=brush.textures[j];
            EMIT_AT(wr.data+(byte*)&brush.textures[j]-(byte*)binding,t);
            EMIT_FIELD(t,anim_next);EMIT_FIELD(t,alternate_anims);
            for(unsigned k=0;k<4;k++)EMIT_FIELD(t,extmemdata[k]);
        }
        for(unsigned j=0;j<world.lump[QBSP_TEXINFO].count;j++)EMIT_FIELD(&brush.texinfo[j],texture);
        for(unsigned j=0;j<world.lump[QBSP_FACES].count;j++){
            msurface_t*s=&brush.surfaces[j];EMIT_FIELD(s,samples);EMIT_FIELD(s,texinfo);EMIT_FIELD(s,plane);
        }
        for(unsigned j=0;j<world.lump[QBSP_NODES].count;j++)EMIT_FIELD(&brush.nodes[j],plane);
        put32(e+60,wr.count);pos=align8(wr.reloc);
    }
    memcpy(out,"QNAT",4);put32(out+4,1);put32(out+8,pos);put32(out+12,count);
    put32(out+16,pak->bytes);put32(out+20,qpak_crc32(pak->image,pak->bytes));put32(out+24,native_abi());put32(out+28,sizeof(void*));
    put32(out+32,qpak_crc32(out+64,pos-64));
    FILE *f=fopen(output,"wb");if(!f)goto fail;
    bool ok=fwrite(out,1,pos,f)==pos;if(fclose(f))ok=false;
    printf("QNAT1 offline: %u models, %zu bytes; native ABI=%08x\n",count,pos,native_abi());
    compile_free();mod_known=NULL;free(out);return ok;
fail:
    compile_free();mod_known=NULL;free(out);return false;
}
#undef EMIT_AT
#undef EMIT_FIELD
bool mg24_image_open_range(const qpak_t *pak,const char *path,size_t offset,size_t length){
    mg24_shutdown();int fd=open(path,O_RDONLY);struct stat st;
    if(fd<0)return false;
    if(fstat(fd,&st) || st.st_size<64 || st.st_size>=0x80000000u){close(fd);return false;}
    if(!length)length=(size_t)st.st_size;
    long page=sysconf(_SC_PAGESIZE);
    if(page<=0 || offset%(size_t)page || offset>(size_t)st.st_size || length>(size_t)st.st_size-offset || length<64){close(fd);return false;}
    size_t size=length;byte *p=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,(off_t)offset);close(fd);
    if(p==MAP_FAILED)return false;
    uint32_t count=u32(p+12);
    if(memcmp(p,"QNAT",4) || u32(p+4)!=1 || u32(p+8)!=size || !count || count>(size-64)/64 ||
       u32(p+16)!=pak->bytes || u32(p+20)!=qpak_crc32(pak->image,pak->bytes) ||
       u32(p+24)!=native_abi() || u32(p+28)!=sizeof(void*) || u32(p+32)!=qpak_crc32(p+64,size-64))goto fail;
    size_t relocations=0;
    for(unsigned i=0;i<count;i++){
        const byte *e=p+64+64*i;uint32_t base=u32(e+48),bytes=u32(e+52),rel=u32(e+56),n=u32(e+60);
        if(!memchr(e,0,48) || base%8 || base<64+64*count || base>size || bytes>size-base ||
           bytes<sizeof(model_t)+sizeof(brush_model_data_t) || rel<base+bytes || rel>size || n>(size-rel)/8)goto fail;
        for(unsigned j=0;j<n;j++){
            uint32_t slot=u32(p+rel+8*j),target=u32(p+rel+8*j+4),off=target&0x7fffffffu;
            if(slot%sizeof(void*) || slot<base || slot-base>bytes-sizeof(void*))goto fail;
            if(off >= ((target&0x80000000u)?pak->bytes:size))goto fail;
            /* Slots must be zero in the artifact. Detect duplicate relocations. */
            uintptr_t original;memcpy(&original,p+slot,sizeof original);if(original)goto fail;
            const void *v=(target&0x80000000u)?pak->image+off:p+off;memcpy(p+slot,&v,sizeof v);
        }
        relocations+=n;
    }
    if(mprotect(p,size,PROT_READ))goto fail;
    native_image=p;native_bytes=size;native_relocs=relocations;native_pak=pak;
    return true;
fail:
    munmap(p,size);return false;
}
bool mg24_bind(const qbsp_t *w){
    if(!native_image || w->pak!=native_pak)return false;
    const byte *dir=w->pak->image+w->pak->directory+w->file.directory_index*16;
    const char *name=(const char*)w->pak->image+w->pak->strings+u32(dir);
    for(unsigned i=0;i<u32(native_image+12);i++){
        const byte *entry=native_image+64+i*64;
        if(!strcmp(name,(const char*)entry))return setup_level(w,(model_t*)(native_image+u32(entry+48)));
    }
    return false;
}

bool mg24_image_open(const qpak_t *pak,const char *path){return mg24_image_open_range(pak,path,0,0);}
