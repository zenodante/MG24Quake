#ifndef QMAC_MG24_CONFIG_H
#define QMAC_MG24_CONFIG_H
/* The optimized MG24 renderer retains its original algorithms and layout
 * optimizations. WIN32 is upstream's portable host C branch, not a Win32 API. */
#ifndef QMAC_RENDER_C
#define QMAC_RENDER_C 1
#endif
#if !QMAC_RENDER_C
#error "Mac requires portable C; ARM assembler is only available on the MCU"
#endif
#define WIN32 1
#define __INLINE inline
#define __STATIC_INLINE static inline
#define TEXTURE_HAS_ANIM_POINTERS 1
#define QMAC_MG24 1
#endif
