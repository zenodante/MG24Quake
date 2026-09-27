#ifndef QRP2350_ENGINE_COMPAT_H
#define QRP2350_ENGINE_COMPAT_H
/*
 * Compiler/platform compatibility for MG24-derived Quake engine translation
 * units built for RP2350.
 *
 * This header is force-included by the RP2350 engine CMake target.  Do not
 * depend on MG24 include order or on QuakeMG24/src/main.h: quoted includes in
 * MG24 headers may resolve relative to their own directory before our platform
 * include directories are searched.
 *
 * Keep this layer limited to compiler/platform vocabulary used by otherwise
 * portable engine code.  Board hardware, DMA, display, input, audio and XIP
 * resource policy belong in their dedicated RP2350 platform interfaces.
 */

#ifndef QUAKE_RP2350
#define QUAKE_RP2350 1
#endif

/* CMSIS compiler vocabulary used by the MG24 engine. */
#ifndef __ASM
#define __ASM __asm
#endif

#ifndef __INLINE
#define __INLINE inline
#endif

#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif

#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE static inline __attribute__((always_inline))
#endif

#ifndef __NO_RETURN
#define __NO_RETURN __attribute__((noreturn))
#endif

#ifndef __WEAK
#define __WEAK __attribute__((weak))
#endif

#ifndef __PACKED
#define __PACKED __attribute__((packed, aligned(1)))
#endif

#ifndef __ALIGNED
#define __ALIGNED(x) __attribute__((aligned(x)))
#endif

#ifndef __USED
#define __USED __attribute__((used))
#endif

#ifndef __UNUSED
#define __UNUSED __attribute__((unused))
#endif

#endif /* QRP2350_ENGINE_COMPAT_H */
