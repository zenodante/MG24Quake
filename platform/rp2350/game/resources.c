#include "quakedef.h"
#include "resource_format.h"
#include "flash_layout.h"
#include "qpak.h"
static const qrn_header_t *resources;
extern model_t *Mod_FindName(char*);
static bool inside(const void *p,size_t bytes){uintptr_t off=(uintptr_t)p-(QRP_XIP_BASE+QRP_ASSET_OFFSET);return off<=resources->size && bytes<=resources->size-off;}
int game_resources_open(const char *path){
    (void)path;resources=(const void*)(QRP_XIP_BASE+QRP_ASSET_OFFSET);
    if(memcmp(resources->magic,"QRN1",4) || resources->version!=QRN_VERSION || resources->abi!=QRN_ABI || resources->base!=(uintptr_t)resources || resources->size<sizeof(*resources) || resources->size>QRP_SAVE_OFFSET-QRP_ASSET_OFFSET || resources->count>4096 || !inside(resources->files,resources->count*sizeof(qrn_file_t)))Sys_Error("Invalid ARM native resource header");
    if(qpak_crc32((byte*)resources+32,resources->size-32)!=resources->crc)Sys_Error("Resource CRC mismatch");
    for(unsigned i=0;i<resources->count;i++){
        const qrn_file_t *f=resources->files+i;
        if(!memchr(f->name,0,sizeof f->name) || !inside(f->data,f->size))Sys_Error("Invalid resource directory");
        if(f->kind>=1 && f->kind<=3){const qrn_brush_t *b=(const void*)f->data;if(f->size<sizeof *b || (b->magic!=QRN_BRUSH_MAGIC && b->magic!=QRN_ALIAS_MAGIC && b->magic!=QRN_SPRITE_MAGIC) || !b->count || b->count>255 || !inside(b->models,b->count*sizeof(model_t)))Sys_Error("Invalid brush entry");}
    }
    printf("ARM XIP resources: %lu bytes, %lu files\n",(unsigned long)resources->size,(unsigned long)resources->count);return 0;
}
byte *game_resource_file(const char *name,unsigned *size){
    for(unsigned i=0;i<resources->count;i++)if(!strcmp(name,resources->files[i].name)){if(size)*size=resources->files[i].size;return (byte*)resources->files[i].data;}
    if(size)*size=0;return NULL;
}
int game_resource_directory(dpackfile_t *file,int index){
    if(index<0 || (unsigned)index>=resources->count)return 0;
    const qrn_file_t *f=resources->files+index;memset(file,0,sizeof *file);memcpy(file->name,f->name,56);file->filepos=(uintptr_t)f->data-(uintptr_t)resources;file->filelen=f->size;return 1;
}
void game_bind_brush(model_t *model,const char *name){
    unsigned size;const qrn_brush_t *b=(const void*)game_resource_file(name,&size);
    if(!b || (b->magic!=QRN_BRUSH_MAGIC && b->magic!=QRN_ALIAS_MAGIC && b->magic!=QRN_SPRITE_MAGIC))Sys_Error("Missing native brush: %s",name);
    int idx=model->nameIdx;*model=b->models[0];model->nameIdx=idx;
    for(unsigned i=1;i<b->count;i++){char local[16];snprintf(local,sizeof local,"*%u",i);model_t *m=Mod_FindName(local);idx=m->nameIdx;*m=b->models[i];m->nameIdx=idx;}
}
byte *game_leaf_pvs(mleaf_t *leaf,model_t *model){
    static byte pvs[(MAX_MAP_LEAFS+7)/8];brush_model_data_t *b=model->brushModelData;
    unsigned count=(b->numleafs+7)/8;if(count>sizeof pvs)Sys_Error("PVS too large");
    if(leaf==b->leafs || leaf->compressed_vis_idx==65535 || !b->visdata){memset(pvs,255,count);return pvs;}
    const byte *src=b->visdata+leaf->compressed_vis_idx;unsigned i=0;
    while(i<count){if(!inside(src,1))Sys_Error("PVS input overflow");byte v=*src++;if(v)pvs[i++]=v;else{if(!inside(src,1))Sys_Error("PVS run missing");unsigned run=*src++;if(!run || run>count-i)Sys_Error("PVS run overflow");memset(pvs+i,0,run);i+=run;}}
    return pvs;
}
