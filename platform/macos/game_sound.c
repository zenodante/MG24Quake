/* Engine sound requests are consumed by the core-1 ADPCM mixer. */
#include "quakedef.h"
#if QRP_FULL_GAME
#include "sound_bridge.h"
#else
#include "service_sdl.h"
#endif
sfx_t game_sfx[MAX_SFX];
sfx_t *known_sfx=game_sfx;
int num_sfx,sound_started;
unsigned game_sound_starts;
static struct {int entity,channel;double until;} voices[QMIX_CHANNELS];
static vec3_t listener;
sfx_t *S_PrecacheSound(char *name){
    for(int i=0;i<num_sfx;i++)if(!strcmp(known_sfx[i].name,name))return known_sfx+i;
    if(num_sfx==MAX_SFX || strlen(name)>=sizeof(known_sfx[0].name))Sys_Error("Sound registry overflow: %s",name);
    char path[MAX_QPATH];snprintf(path,sizeof(path),"sound/%s",name);unsigned size=0;
    byte *data=COM_LoadFileFromExtMem(path,&size);qsound_t sound;
    if(!data || !qsound_open(&sound,data,size)){Con_Printf("Missing/invalid sound: %s\n",name);return NULL;}
    sfx_t *s=known_sfx+num_sfx++;strcpy(s->name,name);s->data=data;s->length=sound.length;s->loopstart=sound.loop;s->initialized=1;return s;
}
void S_Init(void){sound_started=1;num_sfx=0;}
void S_Startup(void){sound_started=1;}
void S_StartSound(int entity,int channel,sfx_t *sfx,vec3_t origin,float vol,float attenuation){
    if(!sfx)return;
    unsigned size;char path[MAX_QPATH];snprintf(path,sizeof(path),"sound/%s",sfx->name);
    byte *data=COM_LoadFileFromExtMem(path,&size);qsound_t sound;if(!data || !qsound_open(&sound,data,size))return;
    int slot=0;double now=mac_time();
    for(int i=0;i<QMIX_CHANNELS;i++){
        if(channel && voices[i].entity==entity && voices[i].channel==channel){slot=i;break;}
        if(voices[i].until<voices[slot].until)slot=i;
    }
    float distance=0;for(int i=0;i<3;i++){float d=origin[i]-listener[i];distance+=d*d;}
    if(entity!=_g->cl.viewentity)vol*=1.0f-sqrtf(distance)*attenuation/1000.0f;
    if(vol<0)vol=0;if(vol>1)vol=1;
    game_sound_starts++;
    mac_sound_start(slot,&sound,(unsigned)(vol*255),(unsigned)(vol*255));
    voices[slot].entity=entity;voices[slot].channel=channel;
    voices[slot].until=sound.loop>=0?1e30:now+(double)sound.length/sound.rate;
}
void S_StopSound(int entity,int channel){for(int i=0;i<QMIX_CHANNELS;i++)if(voices[i].entity==entity && voices[i].channel==channel){mac_sound_stop(i);voices[i].until=0;}}
void S_StopAllSounds(qboolean clear){(void)clear;mac_sound_stop_all();memset(voices,0,sizeof voices);}
void S_Shutdown(void){S_StopAllSounds(true);sound_started=0;}
void S_ClearBuffer(void){S_StopAllSounds(true);}
void S_StaticSound(int index,vec3_t origin,float vol,float attenuation){if(index>0 && index<=num_sfx)S_StartSound(-index,0,known_sfx+index-1,origin,vol/255,attenuation);}
void S_Update(vec3_t origin,vec3_t forward,vec3_t right,vec3_t up){(void)forward;(void)right;(void)up;memcpy(listener,origin,sizeof listener);}
void S_ExtraUpdate(void){}
void S_TouchSound(char *name){S_PrecacheSound(name);}
void S_ClearPrecache(void){}
void S_BeginPrecaching(void){}
void S_EndPrecaching(void){}
void S_LocalSound(char *name){S_StartSound(_g->cl.viewentity,-1,S_PrecacheSound(name),listener,1,0);}
channel_t channels[MAX_CHANNELS];
int total_channels;
void S_Play(void){for(int i=1;i<Cmd_Argc();i++)S_LocalSound(Cmd_Argv(i));}
void S_PlayVol(void){for(int i=1;i+1<Cmd_Argc();i+=2)S_StartSound(_g->cl.viewentity,-1,S_PrecacheSound(Cmd_Argv(i)),listener,Q_atof(Cmd_Argv(i+1)),0);}
void S_SoundInfo_f(void){Con_Printf("Core-1 QAD1 mixer: %d voices, %d Hz\n",QMIX_CHANNELS,QMIX_RATE);}
void S_SoundList(void){for(int i=0;i<num_sfx;i++)Con_Printf("%s: %d samples\n",known_sfx[i].name,known_sfx[i].length);}
void S_StopAllSoundsC(void){S_StopAllSounds(true);}
