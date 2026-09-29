#include "diagnostics.h"
#include "../boards/st7789.h"
#include "font8x8.h"
#include <stdio.h>
#include <string.h>

volatile struct qrp_diagnostic qrp_diagnostic;
static uint32_t fault_stack[256] __attribute__((aligned(8),used));

void qrp_diagnostic_error(const char *message) {
    unsigned i=0;
    while(message[i] && i<sizeof qrp_diagnostic.message-1) {
        qrp_diagnostic.message[i]=message[i];++i;
    }
    qrp_diagnostic.message[i]=0;
    __atomic_store_n(&qrp_diagnostic.ready,1,__ATOMIC_RELEASE);
}

__attribute__((used,noreturn)) void qrp_fault_capture(uint32_t *sp) {
    qrp_diagnostic.sp=(uint32_t)sp;
    qrp_diagnostic.cfsr=*(volatile uint32_t *)0xe000ed28;
    qrp_diagnostic.hfsr=*(volatile uint32_t *)0xe000ed2c;
    qrp_diagnostic.bfar=*(volatile uint32_t *)0xe000ed38;
    qrp_diagnostic.mmfar=*(volatile uint32_t *)0xe000ed34;
    if((uint32_t)sp>=0x20000000 && (uint32_t)sp<=0x20081fe0 &&
       !((uint32_t)sp&3) && !(qrp_diagnostic.cfsr&0x3030)) {
        qrp_diagnostic.lr=sp[5];qrp_diagnostic.pc=sp[6];
    }
    qrp_diagnostic_error("CORE 0 HARDFAULT");
    for(;;)__asm volatile("nop");
}

/* A private emergency stack also permits reporting normal-stack overflow. */
__attribute__((naked,used)) void isr_hardfault(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "movs r1, #0\n"
        "msr msplim, r1\n"
        "ldr r1, =fault_stack + 1024\n"
        "msr msp, r1\n"
        "b qrp_fault_capture\n");
}

void qrp_diagnostic_display(void) {
    char lines[10][40];
    memset(lines,0,sizeof lines);
    snprintf(lines[0],40,"QUAKE CPU ERROR");
    for(unsigned i=0;i<3;++i)
        for(unsigned j=0;j<39 && qrp_diagnostic.message[i*39+j];++j)
            lines[1+i][j]=qrp_diagnostic.message[i*39+j];
    snprintf(lines[4],40,"PC %08lx  LR %08lx",(unsigned long)qrp_diagnostic.pc,(unsigned long)qrp_diagnostic.lr);
    snprintf(lines[5],40,"SP %08lx",(unsigned long)qrp_diagnostic.sp);
    snprintf(lines[6],40,"CFSR %08lx HFSR %08lx",(unsigned long)qrp_diagnostic.cfsr,(unsigned long)qrp_diagnostic.hfsr);
    snprintf(lines[7],40,"BFAR %08lx MMFAR %08lx",(unsigned long)qrp_diagnostic.bfar,(unsigned long)qrp_diagnostic.mmfar);
    snprintf(lines[9],40,"Please photograph this screen.");
    /* Reuse a small scanline, never touch the engine-owned framebuffer. */
    uint16_t row[320];
    st7789_dma_wait();st7789_end_write();
    st7789_set_window(0,20,320,200);st7789_start_write();
    for(unsigned y=0;y<200;++y) {
        for(unsigned x=0;x<320;++x) {
            unsigned line=y/16,col=x/8,ch=line<10?(unsigned char)lines[line][col]:0;
            row[x]=(line<10 && col<39 && y%16<8 && ch<128 &&
                (font8x8_basic[ch][y%16]&(1u<<(x%8))))?0xffff:0;
        }
        st7789_write_dma((const uint8_t *)row,sizeof row);
        st7789_dma_wait();
    }
    st7789_end_write();
}
