#include "qfiles.h"
#include <limits.h>
#include <string.h>

typedef struct {
    qpak_file_t file;
    uint32_t position;
    bool open;
} handle_t;
static const qpak_t *mounted;
static qpak_cache_t cache;
static handle_t handles[QFILES_MAX_HANDLES];

static handle_t *lookup(int handle) {
    if (handle < 1 || handle > QFILES_MAX_HANDLES || !handles[handle-1].open)
        return NULL;
    return &handles[handle-1];
}
void qfiles_mount(const qpak_t *pak) {
    mounted=pak;
    memset(handles,0,sizeof handles);
    memset(&cache,0,sizeof cache);
}
int Sys_FileOpenRead(char *path,int *handle) {
    if (!handle) return -1;
    *handle=-1;
    qpak_file_t file;
    if (!mounted || !path || !qpak_find(mounted,path,&file) || file.size>INT_MAX)
        return -1;
    for (unsigned i=0;i<QFILES_MAX_HANDLES;++i) {
        if (handles[i].open) continue;
        handles[i]=(handle_t){.file=file,.open=true};
        *handle=(int)i+1;
        return (int)file.size;
    }
    return -1;
}
void Sys_FileClose(int handle) {
    handle_t *h=lookup(handle);
    if (h) memset(h,0,sizeof *h);
}
void Sys_FileSeek(int handle,int position) {
    handle_t *h=lookup(handle);
    if (h && position>=0 && (uint32_t)position<=h->file.size)
        h->position=(uint32_t)position;
}
int Sys_FileRead(int handle,void *dest,int count) {
    handle_t *h=lookup(handle);
    if (!h || !dest || count<=0) return 0;
    uint32_t size=h->file.size-h->position;
    if (size>(uint32_t)count) size=(uint32_t)count;
    if (!size || !qpak_read(mounted,&h->file,&cache,h->position,dest,size)) return 0;
    h->position+=size;
    return (int)size;
}
const uint8_t *qfiles_map(int handle,uint32_t offset,size_t length) {
    handle_t *h=lookup(handle);
    return h?qpak_map(mounted,&h->file,offset,length):NULL;
}
int Sys_FileTime(char *path) {
    qpak_file_t file;
    return mounted && path && qpak_find(mounted,path,&file)?1:-1;
}
int Sys_FileOpenWrite(char *path) { (void)path; return -1; }
int Sys_FileWrite(int handle,void *data,int count) {
    (void)handle; (void)data; (void)count; return 0;
}
