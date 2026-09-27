/* Minimal Quake system boundary for RP2350 Phase 1.
 * File I/O is provided by qfiles.c.  Core-1 input/audio integration remains in
 * qservice and will be connected at the engine boundary without replacing it.
 */
#include "quakedef.h"
#include "pico/stdlib.h"
#include <stdarg.h>
#include <stdio.h>

QDFLOAT Sys_FloatTime(void) {
    return (QDFLOAT)((double)time_us_64()*0.000001);
}
void Sys_Printf(char *fmt,...) {
    va_list ap; va_start(ap,fmt); vprintf(fmt,ap); va_end(ap);
}
void Sys_Error(char *fmt,...) {
    va_list ap; va_start(ap,fmt); vprintf(fmt,ap); va_end(ap);
    printf("\n");
    panic("Quake Sys_Error");
}
void Sys_Quit(void) { panic("Quake Sys_Quit"); }
char *Sys_ConsoleInput(void) { return NULL; }
void Sys_Sleep(void) { tight_loop_contents(); }
void Sys_SendKeyEvents(void) { /* qservice event bridge follows in Phase 1. */ }
void Sys_LowFPPrecision(void) {}
void Sys_HighFPPrecision(void) {}
void Sys_SetFPCW(void) {}
void Sys_MakeCodeWriteable(unsigned long startaddr,unsigned long length) {
    (void)startaddr; (void)length;
}
void Sys_DebugLog(char *file,char *fmt,...) { (void)file; (void)fmt; }
void Sys_mkdir(char *path) { (void)path; }
