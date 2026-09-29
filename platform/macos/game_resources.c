#include "quakedef.h"
#include "qbsp.h"
#include "resource_package.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
extern model_t *Mod_FindName(char *name);
static qpak_t game_pak;
static qbsp_t game_world;
static byte *native_image;
static size_t native_bytes,native_relocs,package_bytes;
static const qpak_t *native_pak;
static uint32_t u32(const byte *p){return qres_u32(p);}
#include "native_abi.h"
static void game_native_close(void){if(native_image)munmap(native_image,native_bytes);native_image=NULL;native_bytes=0;}
bool game_native_open(const qpak_t *pak,const char *path,size_t offset,size_t length){
    game_native_close();int fd=open(path,O_RDONLY);struct stat st;
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

int game_resources_open(const char *path){
    int fd=open(path,O_RDONLY);struct stat st;if(fd<0 || fstat(fd,&st))Sys_Error("Cannot open resources: %s",path);
    byte *data=mmap(NULL,st.st_size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);if(data==MAP_FAILED)Sys_Error("Cannot map resources");
    uint32_t qo,qs,no,ns;if(!qres_open(data,st.st_size,&qo,&qs,&no,&ns) || !qpak_open(&game_pak,data+qo,qs) || !game_native_open(&game_pak,path,no,ns))Sys_Error("Invalid/incompatible full game resources");
    package_bytes=st.st_size;
    return 0;
}
void game_resource_report(FILE *f){
    fprintf(f,"  \"resource_package_bytes\": %zu,\n  \"native_readonly_mapping_bytes\": %zu,\n  \"startup_pointer_relocations\": %zu,\n",package_bytes,native_bytes,native_relocs);
}
byte *game_resource_file(const char *name,unsigned *size){
    qpak_file_t f;if(!qpak_find(&game_pak,name,&f)){if(size)*size=0;return NULL;}if(size)*size=f.size;
    return (byte*)qpak_map(&game_pak,&f,0,f.size);
}
int game_resource_directory(dpackfile_t *file,int index){
    if(index<0 || index>=game_pak.files)return 0;
    const byte *entry=game_pak.image+game_pak.directory+16*index;
    memset(file,0,sizeof(*file));snprintf(file->name,sizeof(file->name),"%s",game_pak.image+game_pak.strings+u32(entry));file->filepos=u32(entry+8);file->filelen=u32(entry+12);return 1;
}
void game_bind_brush(model_t *mod,const char *name){
    for(unsigned i=0;i<u32(native_image+12);i++){
        byte *entry=native_image+64+64*i;if(strcmp(name,(char*)entry))continue;
        model_t *root=(model_t*)(native_image+u32(entry+48));int nameIndex=mod->nameIdx;*mod=*root;mod->nameIdx=nameIndex;
        unsigned n=root->brushModelData->numsubmodels;
        model_t *inlines=(model_t*)((byte*)root+((sizeof(model_t)+7)&~7u)+((sizeof(brush_model_data_t)+7)&~7u));
        for(unsigned j=1;j<n;j++){
            char local[16];snprintf(local,sizeof(local),"*%u",j);model_t *dest=Mod_FindName(local);int idx=dest->nameIdx;*dest=inlines[j-1];dest->nameIdx=idx;
        }
        if(!strcmp(name,sv.modelname) && !qbsp_open(&game_world,&game_pak,name,NULL))Sys_Error("Bad world view");
        return;
    }
    Sys_Error("No offline brush model: %s",name);
}
byte *game_leaf_pvs(mleaf_t *leaf,model_t *model){
    static byte pvs[(MAX_MAP_LEAFS+7)/8];
    unsigned index=leaf-model->brushModelData->leafs;
    if(!qbsp_leaf_pvs(&game_world,index,pvs,sizeof pvs))Sys_Error("Invalid PVS");return pvs;
}
