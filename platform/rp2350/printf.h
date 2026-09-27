#ifndef QRP2350_PRINTF_H
#define QRP2350_PRINTF_H
/* RP2350 replacement for QuakeMG24/src/printf.h.
 *
 * quakedef.h includes "printf.h" on embedded builds.  The MG24 implementation
 * then includes its sibling main.h and sharedUsart.h using quoted includes;
 * quoted-include lookup searches QuakeMG24/src before CMake -I directories, so
 * an RP2350 main.h alone cannot intercept that dependency.  Keep the engine
 * API on the Pico side and use libc/Pico stdio instead of the EFR32 USART
 * wrapper.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>

/* Preserve the names exported by the embedded tiny-printf header for engine
 * sources which call them explicitly.  The normal printf/snprintf names are
 * already supplied by stdio.h and Pico SDK's stdio backend.
 */
#define printf_   printf
#define vprintf_  vprintf
#define sprintf_  sprintf
#define vsprintf_ vsprintf
#define snprintf_ snprintf
#define vsnprintf_ vsnprintf

#endif /* QRP2350_PRINTF_H */
