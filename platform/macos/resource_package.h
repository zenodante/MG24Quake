#ifndef QMAC_RESOURCE_PACKAGE_H
#define QMAC_RESOURCE_PACKAGE_H
#include "qpak.h"
#include <string.h>
/* QRES1 uncompressed Mac bundle; fixed header followed by 16 KiB-aligned
 * sections. Sections retain independent formats and source-pair checks. */
static inline uint32_t qres_u32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline bool qres_open(const uint8_t *p,size_t size,uint32_t *qo,uint32_t *qs,uint32_t *no,uint32_t *ns){
    if(size<64 || memcmp(p,"QRES",4) || qres_u32(p+4)!=1 || qres_u32(p+8)!=size || qres_u32(p+12)!=1)return false;
    uint8_t header[64];memcpy(header,p,64);memset(header+40,0,4);
    if(qpak_crc32(header,64)!=qres_u32(p+40))return false;
    for(unsigned i=44;i<64;i++)if(p[i])return false;
    *qo=qres_u32(p+16);*qs=qres_u32(p+20);*no=qres_u32(p+24);*ns=qres_u32(p+28);
    if(*qo!=16384 || !*qs || *qo>size || *qs>size-*qo || !*ns || *no>size || *ns!=size-*no ||
       (uint64_t)*no!=(((uint64_t)*qo+*qs+16383)&~UINT64_C(16383)))return false;
    for(size_t i=64;i<*qo;i++)if(p[i])return false;
    for(size_t i=(size_t)*qo+*qs;i<*no;i++)if(p[i])return false;
    return qpak_crc32(p+*qo,*qs)==qres_u32(p+32) && qpak_crc32(p+*no,*ns)==qres_u32(p+36);
}
#endif
