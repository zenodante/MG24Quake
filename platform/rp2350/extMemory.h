#ifndef QRP_EXTMEMORY_H
#define QRP_EXTMEMORY_H
/* RP2350 replacement for the MG24 external-SPI memory wrapper.
 * Asset pointers are ordinary memory-mapped XIP addresses.  Reads therefore
 * reduce to memcpy/direct dereference and asynchronous SPI-DMA operations are
 * completed synchronously.  This is intentional: Core 1 DMA is reserved for
 * the validated display/audio service and the production asset store is XIP.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define EXT_MEM_BYTE_TIME 0
#define EXT_MEM_ACCESS_TIME 0
#define EXT_MEMORY_HEADER_SIZE 0
#define EXT_MEMORY_READ_ALIGN_SIZE 1

static const uint8_t *qrp_ext_current;

static inline void extMemSetCurrentAddress(uint32_t address) {
    qrp_ext_current=(const uint8_t *)(uintptr_t)address;
}
static inline uint8_t extMemGetByteFromAddress(const void *addr) {
    return *(const uint8_t *)addr;
}
static inline short extMemFlashGetShortFromAddress(const void *addr) {
    short v; memcpy(&v,addr,sizeof v); return v;
}
static inline void *extMemGetDataFromCurrentAddress(void *dest,unsigned int length) {
    if(length) { memcpy(dest,qrp_ext_current,length); qrp_ext_current+=length; }
    return dest;
}
static inline void *extMemGetDataFromAddress(void *dest,void *src,unsigned int length) {
    if(length) memcpy(dest,src,length); return dest;
}
static inline int extMemGetSize(void) { return 16*1024*1024; }
static inline void *extMemStartAsynchDataRead(uint32_t address,void *dest,uint32_t cnt) {
    memcpy(dest,(const void *)(uintptr_t)address,cnt); return dest;
}
static inline void extMemAsynchReadByteFromAddress(uint32_t address) { (void)address; }
static inline void extMemWaitAsynchDataRead(void) {}
static inline int extMemHasAsynchDataReadFinished(void) { return 1; }
static inline void extMemRestoreInterface(void) {}
static inline void extMemInit(void) {}
static inline void extMemStopDMA(void) {}
static inline int extMemGetRemainingBytes(void) { return 0; }

/* Runtime asset programming is forbidden on RP2350.  Keep these declarations
 * unavailable rather than silently implementing the MG24 level-cache policy. */
#endif
