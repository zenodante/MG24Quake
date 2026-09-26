/* Hardware/resource integration diagnostic, not the Quake game executable yet. */
#include "qpak.h"
#include "qservice.h"
#include "font8x8.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

static qpak_t pak;
static qpak_cache_t cache;
static uint8_t palette[768];
/* Diagnostic-only fallback: 250 ms at 441 Hz, independent of the resource image. */
static uint8_t test_pcm[2756];
static void text(uint8_t *frame,unsigned x,unsigned y,const char *s) {
    for(;*s && x+8<=Q_WIDTH;++s,x+=8) {
        unsigned ch=(unsigned char)*s;if(ch>=128)ch='?';
        for(unsigned row=0;row<8 && y+row<Q_HEIGHT;++row)
            for(unsigned col=0;col<8;++col)
                frame[(y+row)*Q_WIDTH+x+col]=(font8x8_basic[ch][row]&(1u<<col))?15:0;
    }
}
int main(void) {
    vreg_set_voltage(VREG_VOLTAGE_1_10);
    set_sys_clock_pll(1200000000u,6,1);
    stdio_init_all();
    bool assets=qpak_open(&pak,(const void *)(XIP_BASE+QPAK_ASSET_OFFSET),QPAK_ASSET_CAPACITY);
    qpak_file_t pal,background,wav;
    qsound_t sound={0};
    for(unsigned i=0;i<sizeof test_pcm;++i)test_pcm[i]=(i%25<12)?176:80;
    const qsound_t test_sound={.pcm=test_pcm,.length=sizeof test_pcm,
                               .rate=11025,.loop=-1,.width=1};
    bool have_background=false,have_sound=false;
    for(unsigned i=0;i<256;++i)palette[i*3]=palette[i*3+1]=palette[i*3+2]=(uint8_t)i;
    palette[45]=palette[46]=palette[47]=255;
    if(assets) {
        if(qpak_find(&pak,"gfx/palette.lmp",&pal) && pal.size==sizeof palette)
            qpak_read(&pak,&pal,&cache,0,palette,sizeof palette);
        have_background=qpak_find(&pak,"gfx/conback.lmp",&background) && background.size==64008;
        if(qpak_find(&pak,"sound/weapons/shotgn2.wav",&wav)) {
            const uint8_t *data=qpak_map(&pak,&wav,0,wav.size);
            have_sound=qsound_wav(&sound,data,wav.size);
        }
    }
    if(!qservice_start())panic("Service core failed to start");
    uint32_t next_frame=0,next_log=0,number=0;
    uint16_t held=0;
    for(;;) {
        qinput_event_t event;
        while(qservice_input_pop(&event)) {
            uint16_t pressed=event.held&~held;
            if(pressed&1u)
                while(!qservice_sound_start(0,have_sound?&sound:&test_sound,255,255))tight_loop_contents();
            if(pressed&2u)while(!qservice_sound_stop(0))tight_loop_contents();
            if(pressed&4u)
                while(!qservice_sound_start(0,&test_sound,255,255))tight_loop_contents();
            held=event.held;
        }
        held=qservice_buttons();
        uint32_t now=time_us_32();
        if((int32_t)(now-next_frame)>=0) {
            unsigned slot;uint8_t *frame=qservice_frame_acquire(&slot);
            if(frame) {
                bool bg=have_background && qpak_read(&pak,&background,&cache,8,frame,Q_FRAME_BYTES);
                if(!bg)for(unsigned y=0;y<Q_HEIGHT;++y)for(unsigned x=0;x<Q_WIDTH;++x)
                    frame[y*Q_WIDTH+x]=(uint8_t)((x+y+number)&255);
                text(frame,8,8,"RP2350 200MHZ / CORE1 IO TEST");
                text(frame,8,24,assets?"PAK OK - ALL 3 DEMOS RETAINED":"NO QPAK AT FLASH OFFSET 1MB");
                text(frame,8,40,"A SOUND / B STOP / X TEST TONE");
                text(frame,8,56,"ENGINE INTEGRATION PENDING");
                text(frame,8,72,have_sound?"SHOTGUN READY / 320X200":
                     "NO WAV: A/X USE BUILT-IN TONE");
                for(unsigned i=0;i<10;++i) {
                    char label[4];snprintf(label,sizeof label,"%u",i);
                    unsigned x=8+i*30;
                    for(unsigned y=170;y<190;++y)memset(frame+y*Q_WIDTH+x,(held&(1u<<i))?15:32,24);
                    text(frame,x+4,176,label);
                }
                unsigned x=number%300;
                for(unsigned y=120;y<128;++y)memset(frame+y*Q_WIDTH+x,15,16);
                qservice_frame_submit(slot,palette);++number;next_frame=now+33333;
            }
        }
        if((int32_t)(now-next_log)>=0) {
            qservice_stats_t s;qservice_stats(&s);
            printf("core0 %luMHz pak=%d wav=%d buttons=%03x bytes=%lu frames=%lu audio=%lu underruns=%lu input_errors=%lu overflows=%lu\n",
                (unsigned long)(clock_get_hz(clk_sys)/1000000),assets,have_sound,held,(unsigned long)pak.bytes,
                (unsigned long)s.frames,(unsigned long)s.audio_blocks,(unsigned long)s.audio_underruns,
                (unsigned long)s.input_errors,(unsigned long)s.input_overflows);
            next_log=now+1000000;
        }
        tight_loop_contents();
    }
}
