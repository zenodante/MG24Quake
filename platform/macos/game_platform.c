#include "quakedef.h"
#include "service_sdl.h"
#include <stdarg.h>
#include <execinfo.h>
static byte palette_copy[768];
void Sys_Error(char *fmt,...){va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);fputc('\n',stderr);void *trace[40];int n=backtrace(trace,40);backtrace_symbols_fd(trace,n,2);exit(1);}
void Sys_Printf(char *fmt,...){va_list ap;va_start(ap,fmt);vprintf(fmt,ap);va_end(ap);}
QDFLOAT Sys_FloatTime(void){static double epoch;double now=mac_time();if(!epoch)epoch=now;return now-epoch;}
void Sys_Quit(void){mac_request_quit();}
void Sys_LowFPPrecision(void){}
void Sys_HighFPPrecision(void){}
char *Sys_ConsoleInput(void){return NULL;}
void IN_Init(void){}
void IN_Shutdown(void){}
void IN_Commands(void){}
void IN_Move(usercmd_t *cmd){(void)cmd;}
void Sys_SendKeyEvents(void){
    int key;bool down;
    while(mac_key_event(&key,&down)){
        switch(key){
        case SDLK_UP:key=K_UPARROW;break;case SDLK_DOWN:key=K_DOWNARROW;break;
        case SDLK_LEFT:key=K_LEFTARROW;break;case SDLK_RIGHT:key=K_RIGHTARROW;break;
        case SDLK_LCTRL:case SDLK_RCTRL:key=K_CTRL;break;
        case SDLK_LSHIFT:case SDLK_RSHIFT:key=K_SHIFT;break;
        case SDLK_LALT:case SDLK_RALT:key=K_ALT;break;
        case SDLK_BACKSPACE:key=K_BACKSPACE;break;
        case SDLK_F1:key=K_F1;break;case SDLK_F2:key=K_F2;break;case SDLK_F3:key=K_F3;break;
        case SDLK_F4:key=K_F4;break;case SDLK_F5:key=K_F5;break;case SDLK_F6:key=K_F6;break;
        case SDLK_F9:key=K_F9;break;case SDLK_F10:key=K_F10;break;case SDLK_F12:key=K_F12;break;
        case 0x20000001:key=K_MOUSE1;break;case 0x20000003:key=K_MOUSE2;break;
        default:if(key<0 || key>127)continue;
        }
        Key_Event(key,down);
    }
}
void VID_SetPalette(byte *palette){memcpy(palette_copy,palette,768);}
void VID_ShiftPalette(byte *palette){VID_SetPalette(palette);}
void VID_Init(byte *palette){
    VID_SetPalette(palette);vid.buffer=mac_frame_acquire();vid.conbuffer=vid.buffer;
    vid.colormap=host_colormap;vid.fullbright=256-((int*)vid.colormap)[2048];vid.aspect=1.0f;
}
void VID_Update(vrect_t *rects){if(rects && rects->height>DRAW_BUFFER_HEIGHT)memcpy(vid.buffer+SCREEN_WIDTH*DRAW_BUFFER_HEIGHT,aux_buffer,SCREEN_WIDTH*(SCREEN_HEIGHT-DRAW_BUFFER_HEIGHT));mac_frame_submit(palette_copy);byte *next=mac_frame_acquire();if(next)vid.buffer=next;vid.conbuffer=vid.buffer;}
void VID_Shutdown(void){}
qboolean SNDDMA_Init(void){return false;}
int SNDDMA_GetDMAPos(void){return 0;}
