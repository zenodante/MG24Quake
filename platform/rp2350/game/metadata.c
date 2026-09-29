/* Bounded compatibility store for the remaining MG24 UI/model registries.
 * This is SRAM, never a Flash writer. Brush geometry lives entirely in XIP. */
#include "quakedef.h"
#include "internalFlash.h"
static _Alignas(4) byte metadata[32*1024];
static unsigned used,common,peak;
void internalFlashInit(void){used=common=peak=0;}
void internalFlashSetCommonZone(void){common=used;}
void internalFlashResetToCommonZoneEnd(void){used=common;}
void eraseInternalFlash(int sections){(void)sections;}
void *getCurrentInternalFlashPtr(void){return metadata+used;}
int getInternalFlashRemaningSize(void){return sizeof metadata-used;}
void *reserveInternalFlashSize(int size){
    if(size<0 || (size&3) || (unsigned)size>sizeof metadata-used)Sys_Error("Metadata SRAM overflow: used=%u request=%d",used,size);
    void *p=metadata+used;used+=size;if(used>peak)peak=used;return p;
}
void *storeToInternalFlash2(void *source,int size,char *fn,int line){(void)fn;(void)line;void *p=reserveInternalFlashSize(size);memcpy(p,source,size);return p;}
void *storeToInternalFlashAtPointer(void *source,void *dest,int size){
    uintptr_t off=(uintptr_t)dest-(uintptr_t)metadata;
    if(size<0 || off>used || (unsigned)size>used-off)Sys_Error("Metadata write outside reservation");
    memcpy(dest,source,size);return dest;
}
unsigned qrp_metadata_peak(void){return peak;}
