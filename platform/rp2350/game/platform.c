#include "quakedef.h"
#include "qpak.h"
#include "qservice.h"
#include "flash_layout.h"
#include <stdarg.h>
#include "diagnostics.h"
extern bool stdio_init_all(void);
extern uint64_t time_us_64(void);
static byte palette[768];
static unsigned frame_slot;
const char *qmac_game_pak="fixed XIP";
void Sys_Error(char *fmt,...){char message[120];va_list ap;va_start(ap,fmt);vsnprintf(message,sizeof message,fmt,ap);va_end(ap);qrp_diagnostic_error(message);printf("%s\n",message);for(;;)__asm volatile("nop");}
void Sys_Printf(char *fmt,...){va_list ap;va_start(ap,fmt);vprintf(fmt,ap);va_end(ap);}
QDFLOAT Sys_FloatTime(void){return (double)time_us_64()*1e-6;}
void Sys_Quit(void){exit(0);}
void Sys_LowFPPrecision(void){}
void Sys_HighFPPrecision(void){}
extern int stdio_getchar_timeout_us(uint32_t timeout);
char *Sys_ConsoleInput(void){
    static char line[64];static unsigned used;int ch;
    while((ch=stdio_getchar_timeout_us(0))>=0){
        if(ch=='\r' || ch=='\n'){if(used){line[used++]='\n';line[used]=0;used=0;return line;}}
        else if(ch>=32 && ch<127 && used<sizeof line-2)line[used++]=ch;
    }return NULL;
}
void IN_Init(void){}
void IN_Shutdown(void){}
void IN_Commands(void){}
void IN_Move(usercmd_t *cmd){(void)cmd;}
void Sys_SendKeyEvents(void){
    static uint16_t previous;uint16_t now=qservice_buttons(),pressed=qservice_buttons_pressed();
    /* QwSTPad bit order: A, B, X, Y, Up, Down, Left, Right, Plus, Minus. */
    static const int keys[]={'d',K_CTRL,'v','a',K_UPARROW,K_DOWNARROW,K_LEFTARROW,K_RIGHTARROW,K_PGUP,K_PGDN};
    for(unsigned i=0;i<10;i++){
        unsigned mask=1u<<i;
        if((now^previous)&mask)Key_Event(keys[i],(now>>i)&1);
        else if((pressed&mask) && !(now&mask)){Key_Event(keys[i],true);Key_Event(keys[i],false);}
    }
    previous=now;
}
void VID_SetPalette(byte *p){memcpy(palette,p,768);}
void VID_ShiftPalette(byte *p){VID_SetPalette(p);}
static byte *acquire_frame(void){byte *p;while(!(p=qservice_frame_acquire(&frame_slot))){__asm volatile("nop");}return p;}
void VID_Init(byte *p){VID_SetPalette(p);vid.buffer=acquire_frame();vid.conbuffer=vid.buffer;vid.colormap=host_colormap;vid.fullbright=256-((int*)vid.colormap)[2048];vid.aspect=1;}
void VID_Update(vrect_t *r){if(r && r->height>DRAW_BUFFER_HEIGHT)memcpy(vid.buffer+SCREEN_WIDTH*DRAW_BUFFER_HEIGHT,aux_buffer,SCREEN_WIDTH*(SCREEN_HEIGHT-DRAW_BUFFER_HEIGHT));qservice_frame_submit(frame_slot,palette);vid.buffer=acquire_frame();vid.conbuffer=vid.buffer;}
void VID_Shutdown(void){}
qboolean SNDDMA_Init(void){return false;}
int SNDDMA_GetDMAPos(void){return 0;}
double mac_time(void){return (double)time_us_64()*1e-6;}
bool mac_sound_start(unsigned n,const qsound_t*s,unsigned l,unsigned r){return qservice_sound_start(n,s,l,r);}
void mac_sound_stop(unsigned n){qservice_sound_stop(n);}
void mac_sound_stop_all(void){qservice_sound_stop_all(0);}
typedef struct {uint32_t frames,signon,entities,health,zone_peak,metadata_peak,shells,sound_starts,server_active;char map[32];} qrp_game_stats_t;
volatile qrp_game_stats_t qrp_game_stats;
extern unsigned qrp_metadata_peak(void);
extern unsigned game_sound_starts;
int main(void){
    stdio_init_all();if(!qservice_start())Sys_Error("Core 1 peripheral startup failed");memset(_g,0,sizeof *_g);Z_Init();Cvar_Init();Host_Init(NULL);_g->cls.demonum=-1;Cbuf_Execute();
    const char *bindings[]={
        "bind LEFTARROW +left", "bind RIGHTARROW +right",
        "bind UPARROW +forward", "bind DOWNARROW +back",
        "bind a +moveleft", "bind d +moveright",
        "bind CTRL +attack", "bind v \"impulse 10\"",
        "bind PGDN +lookdown", "bind PGUP +lookup"
    };
    for(unsigned i=0;i<sizeof bindings/sizeof bindings[0];i++)Cmd_ExecuteString(bindings[i],src_command);
    Cbuf_AddText("map start\n");
    double previous=Sys_FloatTime();
    for(;;){double now=Sys_FloatTime(),dt=now-previous;previous=now;if(dt>0.1)dt=0.1;Host_Frame(dt);
        qrp_game_stats.frames++;qrp_game_stats.signon=_g->cls.signon;qrp_game_stats.entities=sv.num_edicts;qrp_game_stats.health=_g->cl.stats[STAT_HEALTH];
        unsigned zone=MAX_STATIC_ZONE-getZoneRemainingSize();if(zone>qrp_game_stats.zone_peak)qrp_game_stats.zone_peak=zone;
        qrp_game_stats.metadata_peak=qrp_metadata_peak();qrp_game_stats.shells=_g->cl.stats[STAT_SHELLS];qrp_game_stats.sound_starts=game_sound_starts;qrp_game_stats.server_active=sv.active;memcpy((void*)qrp_game_stats.map,sv.name,sizeof qrp_game_stats.map);
    }
}
