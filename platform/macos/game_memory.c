/* Optional host-only allocation observer. Never linked into a target build. */
#if QMAC_MEMORY_AUDIT
#include "quakedef.h"
#include <dlfcn.h>
#include <execinfo.h>
typedef struct {void *ptr;size_t bytes;const char *owner;bool flash;} allocation_t;
typedef struct {const char *owner;size_t bytes,count;} group_t;
static allocation_t allocations[8192];
static group_t peaks[2][256];
static unsigned peak_groups[2];
static size_t current[2],highwater[2];
static const char *caller(void){
    void *stack[16];int n=backtrace(stack,16);
    for(int i=2;i<n;i++){
        Dl_info info;if(!dladdr(stack[i],&info) || !info.dli_sname)continue;
        const char *s=info.dli_sname;
        if(!strncmp(s,"Z_",2) || !strncmp(s,"qmac_profile_",13) || !strcmp(s,"reserveInternalFlashSize") || !strcmp(s,"storeToInternalFlash2"))continue;
        return s;
    }
    return "unknown";
}
static void snapshot(bool flash){
    unsigned n=0;
    memset(peaks[flash],0,sizeof peaks[flash]);
    for(unsigned i=0;i<8192;i++)if(allocations[i].ptr && allocations[i].flash==flash){
        allocation_t *a=&allocations[i];unsigned j;
        for(j=0;j<n;j++)if(!strcmp(peaks[flash][j].owner,a->owner))break;
        if(j==n){if(n==256)Sys_Error("Memory observer group overflow");peaks[flash][n++].owner=a->owner;}
        peaks[flash][j].bytes+=a->bytes;peaks[flash][j].count++;
    }
    peak_groups[flash]=n;
}
void qmac_profile_alloc(void *ptr,size_t bytes,bool flash,const char *owner){
    if(!ptr)return;
    for(unsigned i=0;i<8192;i++)if(!allocations[i].ptr){
        allocations[i]=(allocation_t){ptr,bytes,owner?owner:caller(),flash};current[flash]+=bytes;
        if(current[flash]>highwater[flash]){highwater[flash]=current[flash];snapshot(flash);}return;
    }
    Sys_Error("Memory observer allocation overflow");
}
void qmac_profile_free(void *ptr){
    if(!ptr)return;
    for(unsigned i=0;i<8192;i++)if(allocations[i].ptr==ptr && !allocations[i].flash){current[0]-=allocations[i].bytes;allocations[i].ptr=NULL;return;}
    Sys_Error("Memory observer saw untracked zone free");
}
void qmac_profile_flash_reset(void *end){
    for(unsigned i=0;i<8192;i++)if(allocations[i].ptr && allocations[i].flash && (uintptr_t)allocations[i].ptr>=(uintptr_t)end){current[1]-=allocations[i].bytes;allocations[i].ptr=NULL;}
}
void qmac_memory_report(FILE *f){
    fprintf(f,"  \"observer_static_bytes_excluded\": %zu,\n",sizeof allocations+sizeof peaks+sizeof peak_groups+sizeof current+sizeof highwater);
    for(int k=0;k<2;k++){
        fprintf(f,"  \"%s\": {\"peak_bytes\": %zu, \"groups\": [",k?"legacy_flash_peak_allocations":"zone_peak_allocations",highwater[k]);
        for(unsigned i=0;i<peak_groups[k];i++)fprintf(f,"%s{\"caller\": \"%s\", \"bytes\": %zu, \"blocks\": %zu}",i?", ":"",peaks[k][i].owner,peaks[k][i].bytes,peaks[k][i].count);
        fprintf(f,"]},\n");
    }
}
#endif
