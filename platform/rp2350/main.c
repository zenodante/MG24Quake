/* Hardware/resource integration diagnostic, not the Quake game executable yet. */
#include "qpak.h"
#include "qfiles.h"
#include "qbsp.h"
#include "qrender.h"
#include "qcollision.h"
#include "qservice.h"
#include "font8x8.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static qpak_t pak;
static qbsp_t world;
static qr_renderer_t renderer;
static qr_camera_t camera;
static qc_workspace_t collision_work;
static qpak_cache_t world_cache;
static uint8_t palette[768];
static uint8_t test_pcm[2756];
static void text(uint8_t *frame,unsigned x,unsigned y,const char *s){for(;*s&&x+8<=Q_WIDTH;++s,x+=8){unsigned ch=(unsigned char)*s;if(ch>=128)ch='?';for(unsigned row=0;row<8&&y+row<Q_HEIGHT;++row)for(unsigned col=0;col<8;++col)frame[(y+row)*Q_WIDTH+x+col]=(font8x8_basic[ch][row]&(1u<<col))?15:0;}}
int main(void){
    vreg_set_voltage(VREG_VOLTAGE_1_10);set_sys_clock_pll(1200000000u,6,1);stdio_init_all();
    bool assets=qpak_open(&pak,(const void *)(XIP_BASE+QPAK_ASSET_OFFSET),QPAK_ASSET_CAPACITY);qfiles_mount(assets?&pak:NULL);
    int pal=-1,background=-1,wav=-1;qsound_t sound={0};for(unsigned i=0;i<sizeof test_pcm;++i)test_pcm[i]=(i%25<12)?176:80;
    const qsound_t test_sound={.data=test_pcm,.bytes=sizeof test_pcm,.length=sizeof test_pcm,.rate=11025,.loop=-1,.width=1,.format=QSOUND_PCM};
    bool have_background=false,have_sound=false;for(unsigned i=0;i<256;++i)palette[i*3]=palette[i*3+1]=palette[i*3+2]=(uint8_t)i;palette[45]=palette[46]=palette[47]=255;
    if(assets){if(Sys_FileOpenRead("gfx/palette.lmp",&pal)==sizeof palette)Sys_FileRead(pal,palette,sizeof palette);Sys_FileClose(pal);have_background=Sys_FileOpenRead("gfx/conback.lmp",&background)==64008;int sound_size=Sys_FileOpenRead("sound/weapons/shotgn2.wav",&wav);if(sound_size>=0){const uint8_t *data=qfiles_map(wav,0,(size_t)sound_size);have_sound=data&&qsound_open(&sound,data,(size_t)sound_size);}Sys_FileClose(wav);}
    if(!qservice_start())panic("Service core failed to start");bool have_world=assets&&qbsp_open(&world,&pak,"maps/start.bsp",&world_cache);bool have_renderer=have_world&&qr_init(&renderer,&world)&&qr_spawn_camera(&world,&camera);bool show_world=have_renderer,collision_enabled=true;unsigned move_errors=0;uint32_t next_frame=0,next_log=0,number=0,last_input=time_us_32(),render_us=0;
    for(;;){
        uint16_t pressed=qservice_buttons_pressed();if(pressed&1u)qservice_sound_start(0,have_sound?&sound:&test_sound,255,255);if(pressed&2u)qservice_sound_stop(0);if(pressed&4u)qservice_sound_start(0,&test_sound,255,255);uint16_t held=qservice_buttons();uint32_t now=time_us_32();float dt=fminf((uint32_t)(now-last_input)*.000001f,.1f);last_input=now;if((pressed&8u)&&have_renderer)show_world=!show_world;
        if(show_world){camera.yaw+=((held&128u?1:0)-(held&64u?1:0))*90*dt;camera.yaw=fmodf(camera.yaw+360,360);float step=((held&16u?1:0)-(held&32u?1:0))*100*dt;float angle=camera.yaw*.01745329252f;if((held&768u)==768u&&(pressed&768u)){collision_enabled=!collision_enabled;if(collision_enabled){float origin[3]={camera.position[0],camera.position[1],camera.position[2]-22};int32_t contents;if(!qbsp_point_contents(&world,0,1,origin,&contents)||contents==-2)qr_spawn_camera(&world,&camera);}}float movement[3]={cosf(angle)*step,sinf(angle)*step,((held&256u?1:0)-(held&512u?1:0))*100*dt};if(collision_enabled){float origin[3]={camera.position[0],camera.position[1],camera.position[2]-22};if(qc_slide(&collision_work,&world,origin,movement)){for(unsigned k=0;k<3;++k)camera.position[k]=origin[k];camera.position[2]+=22;}else ++move_errors;}else for(unsigned k=0;k<3;++k)camera.position[k]+=movement[k];}
        if((int32_t)(now-next_frame)>=0){unsigned slot;uint8_t *frame=qservice_frame_acquire(&slot);if(frame){if(show_world){uint32_t rs=time_us_32();bool complete=qr_draw(&renderer,&camera,frame);render_us=time_us_32()-rs;memset(frame+QR_WIDTH*QR_HEIGHT,0,Q_FRAME_BYTES-QR_WIDTH*QR_HEIGHT);text(frame,8,156,complete?(collision_enabled?"WORLD / HULL COLLISION":"WORLD / NOCLIP"):"WORLD VIEW: RENDER LIMIT/ERROR");text(frame,8,172,"DPAD MOVE/TURN +/- Z BOTH NOCLIP");char timing[40];snprintf(timing,sizeof timing,"%lu MS  %u FACES  A SOUND / B STOP",(unsigned long)(render_us/1000),renderer.stats.faces);text(frame,8,188,timing);}else{if(have_background)Sys_FileSeek(background,8);bool bg=have_background&&Sys_FileRead(background,frame,Q_FRAME_BYTES)==Q_FRAME_BYTES;if(!bg)for(unsigned y=0;y<Q_HEIGHT;++y)for(unsigned x=0;x<Q_WIDTH;++x)frame[y*Q_WIDTH+x]=(uint8_t)((x+y+number)&255);text(frame,8,8,"RP2350 200MHZ / CORE1 IO TEST");text(frame,8,24,assets?"PAK OK - ALL 3 DEMOS RETAINED":"NO QPAK AT FLASH OFFSET 1MB");text(frame,8,40,"A SOUND / B STOP / X TEST TONE");char ms[40];if(have_world)snprintf(ms,sizeof ms,"START BSP: %lu FACES / %lu LEAVES",(unsigned long)world.lump[QBSP_FACES].count,(unsigned long)world.lump[QBSP_LEAVES].count);else snprintf(ms,sizeof ms,"START BSP MISSING OR INVALID");text(frame,8,56,ms);text(frame,8,72,have_sound?(sound.format==QSOUND_QAD1?"SHOTGUN QAD1 READY / 320X200":"SHOTGUN PCM READY / 320X200"):"NO SOUND: A/X USE BUILT-IN TONE");if(have_renderer)text(frame,8,88,"Y: RETURN TO WORLD VIEW");for(unsigned i=0;i<10;++i){char label[4];snprintf(label,sizeof label,"%u",i);unsigned x=8+i*30;for(unsigned y=170;y<190;++y)memset(frame+y*Q_WIDTH+x,(held&(1u<<i))?15:32,24);text(frame,x+4,176,label);}unsigned x=number%300;for(unsigned y=120;y<128;++y)memset(frame+y*Q_WIDTH+x,15,16);}qservice_frame_submit(slot,palette);++number;next_frame=now+33333;}}
        if((int32_t)(now-next_log)>=0){qservice_stats_t s;qservice_stats(&s);printf("core0 %luMHz pak=%d sound=%d fmt=%u buttons=%03x bytes=%lu frames=%lu audio=%lu underruns=%lu input_errors=%lu overflows=%lu\n",(unsigned long)(clock_get_hz(clk_sys)/1000000),assets,have_sound,have_sound?(unsigned)sound.format:99u,held,(unsigned long)pak.bytes,(unsigned long)s.frames,(unsigned long)s.audio_blocks,(unsigned long)s.audio_underruns,(unsigned long)s.input_errors,(unsigned long)s.input_overflows);if(have_world)printf("bsp29 start: models=%lu vertices=%lu faces=%lu leaves=%lu pvs_bytes=%lu\n",(unsigned long)world.lump[QBSP_MODELS].count,(unsigned long)world.lump[QBSP_VERTICES].count,(unsigned long)world.lump[QBSP_FACES].count,(unsigned long)world.lump[QBSP_LEAVES].count,(unsigned long)qbsp_pvs_bytes(&world));if(have_renderer)printf("world render_us=%lu faces=%u triangles=%u pixels=%u rejected=%u\n",(unsigned long)render_us,renderer.stats.faces,renderer.stats.triangles,renderer.stats.pixels,renderer.stats.rejected);if(have_renderer)printf("movement collision=%d errors=%u (no gravity/steps/entities)\n",collision_enabled,move_errors);next_log=now+1000000;}
        tight_loop_contents();
    }
}
