/* LINK/SIZE AUDIT ONLY, not bootable firmware. The engine and hardware service
 * are real; target-native resource binding is deliberately a fail-fast stub. */
#include "quakedef.h"
#include "qpak.h"
#include "qbsp.h"
#include "qservice.h"
#include "flash_layout.h"
#include <stdarg.h>
extern bool stdio_init_all(void);
extern uint64_t time_us_64(void);
static qpak_t resources;
static qbsp_t world;
static byte palette[768];
static unsigned frame_slot;
const char *qmac_game_pak="fixed XIP";
void Sys_Error(char *fmt,...){va_list ap;va_start(ap,fmt);vprintf(fmt,ap);va_end(ap);exit(1);}
void Sys_Printf(char *fmt,...){va_list ap;va_start(ap,fmt);vprintf(fmt,ap);va_end(ap);}
QDFLOAT Sys_FloatTime(void){return (double)time_us_64()*1e-6;}
void Sys_Quit(void){exit(0);}
void Sys_LowFPPrecision(void){}
void Sys_HighFPPrecision(void){}
char *Sys_ConsoleInput(void){return NULL;}
void IN_Init(void){}
void IN_Shutdown(void){}
void IN_Commands(void){}
void IN_Move(usercmd_t *cmd){(void)cmd;}
void Sys_SendKeyEvents(void){
    static uint16_t previous;uint16_t now=qservice_buttons();
    static const int keys[]={K_UPARROW,K_DOWNARROW,K_LEFTARROW,K_RIGHTARROW,K_CTRL,K_SPACE,K_ESCAPE,K_ENTER};
    for(unsigned i=0;i<8;i++)if((now^previous)&(1u<<i))Key_Event(keys[i],(now>>i)&1);
    previous=now;
}
void VID_SetPalette(byte *p){memcpy(palette,p,768);}
void VID_ShiftPalette(byte *p){VID_SetPalette(p);}
void VID_Init(byte *p){VID_SetPalette(p);vid.buffer=qservice_frame_acquire(&frame_slot);vid.conbuffer=vid.buffer;vid.colormap=host_colormap;vid.fullbright=256-((int*)vid.colormap)[2048];vid.aspect=1;}
void VID_Update(vrect_t *r){if(r && r->height>DRAW_BUFFER_HEIGHT)memcpy(vid.buffer+SCREEN_WIDTH*DRAW_BUFFER_HEIGHT,aux_buffer,SCREEN_WIDTH*(SCREEN_HEIGHT-DRAW_BUFFER_HEIGHT));qservice_frame_submit(frame_slot,palette);vid.buffer=qservice_frame_acquire(&frame_slot);vid.conbuffer=vid.buffer;}
void VID_Shutdown(void){}
qboolean SNDDMA_Init(void){return false;}
int SNDDMA_GetDMAPos(void){return 0;}
double mac_time(void){return (double)time_us_64()*1e-6;}
bool mac_sound_start(unsigned n,const qsound_t*s,unsigned l,unsigned r){return qservice_sound_start(n,s,l,r);}
void mac_sound_stop(unsigned n){qservice_sound_stop(n);}
void mac_sound_stop_all(void){qservice_sound_stop_all(0);}
int game_resources_open(const char *path){(void)path;if(!qpak_open(&resources,(const byte*)(QRP_XIP_BASE+QRP_ASSET_OFFSET),QRP_SAVE_OFFSET-QRP_ASSET_OFFSET))Sys_Error("Invalid XIP");return 0;}
byte *game_resource_file(const char *name,unsigned *size){qpak_file_t f;if(!qpak_find(&resources,name,&f)){if(size)*size=0;return NULL;}if(size)*size=f.size;return (byte*)qpak_map(&resources,&f,0,f.size);}
int game_resource_directory(dpackfile_t *file,int index){
    if(index<0 || index>=resources.files)return 0;
    const uint32_t *e=(const uint32_t*)(resources.image+resources.directory+16*index);
    memset(file,0,sizeof *file);snprintf(file->name,sizeof file->name,"%s",resources.image+resources.strings+e[0]);file->filepos=e[2];file->filelen=e[3];return 1;
}
void game_bind_brush(model_t *model,const char *name){
    (void)model;qbsp_open(&world,&resources,name,NULL);
    Sys_Error("Size audit only: target-native model binder is not implemented");
}
byte *game_leaf_pvs(mleaf_t *leaf,model_t *model){
    static byte pvs[(MAX_MAP_LEAFS+7)/8];
    qbsp_leaf_pvs(&world,(unsigned)(leaf-model->brushModelData->leafs),pvs,sizeof pvs);return pvs;
}
int main(void){
    stdio_init_all();qservice_start();memset(_g,0,sizeof *_g);Z_Init();Cvar_Init();Host_Init(NULL);
    Cbuf_AddText("map start\n");for(;;)Host_Frame(1.0f/60.0f);
}
