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
enum { DYNAMIC_VOICES=4, STATIC_VOICES=2, AMBIENT_FIRST=6, STATIC_LIMIT=128 };
static struct {int entity,channel;double until;vec3_t origin;float vol,attenuation;} voices[DYNAMIC_VOICES];
static struct {sfx_t *sfx;vec3_t origin;float vol,attenuation;} static_sounds[STATIC_LIMIT];
static unsigned static_count;
static int static_selected[STATIC_VOICES]={-1,-1};
static sfx_t *ambient_sfx[2];
static bool ambient_running[2];
static float ambient_gain[2];
extern bool qservice_sound_gain(unsigned,unsigned,unsigned);
static vec3_t listener;
sfx_t *S_PrecacheSound(char *name){
    for(int i=0;i<num_sfx;i++)if(!strcmp(known_sfx[i].name,name))return known_sfx+i;
    if(num_sfx==MAX_SFX || strlen(name)>=sizeof(known_sfx[0].name))Sys_Error("Sound registry overflow: %s",name);
    char path[MAX_QPATH];snprintf(path,sizeof(path),"sound/%s",name);unsigned size=0;
    byte *data=COM_LoadFileFromExtMem(path,&size);qsound_t sound;
    if(!data || !qsound_open(&sound,data,size)){Con_Printf("Missing/invalid sound: %s\n",name);return NULL;}
    sfx_t *s=known_sfx+num_sfx++;strcpy(s->name,name);s->data=data;s->length=sound.length;s->loopstart=sound.loop;s->initialized=1;return s;
}
void S_Init(void){sound_started=1;num_sfx=0;ambient_sfx[0]=S_PrecacheSound("ambience/water1.wav");ambient_sfx[1]=S_PrecacheSound("ambience/wind2.wav");}
void S_Startup(void){sound_started=1;}
void S_StartSound(int entity,int channel,sfx_t *sfx,vec3_t origin,float vol,float attenuation){
    if(!sfx)return;
    unsigned size;char path[MAX_QPATH];snprintf(path,sizeof(path),"sound/%s",sfx->name);
    byte *data=COM_LoadFileFromExtMem(path,&size);qsound_t sound;if(!data || !qsound_open(&sound,data,size))return;
    int slot=0;double now=mac_time();
    for(int i=0;i<DYNAMIC_VOICES;i++){
        if(channel && voices[i].entity==entity && voices[i].channel==channel){slot=i;break;}
        if(voices[i].until<voices[slot].until)slot=i;
    }
    memcpy(voices[slot].origin,origin,sizeof(vec3_t));voices[slot].vol=vol;voices[slot].attenuation=attenuation;
    vol*=volume;
    float distance=0;for(int i=0;i<3;i++){float d=origin[i]-listener[i];distance+=d*d;}
    if(entity!=_g->cl.viewentity)vol*=1.0f-sqrtf(distance)*attenuation/1000.0f;
    if(vol<0)vol=0;if(vol>1)vol=1;
    game_sound_starts++;
    mac_sound_start(slot,&sound,(unsigned)(vol*255),(unsigned)(vol*255));
    voices[slot].entity=entity;voices[slot].channel=channel;
    voices[slot].until=sound.loop>=0?1e30:now+(double)sound.length/sound.rate;
}
void S_StopSound(int entity,int channel){for(int i=0;i<DYNAMIC_VOICES;i++)if(voices[i].entity==entity && voices[i].channel==channel){mac_sound_stop(i);voices[i].until=0;}}
void S_StopAllSounds(qboolean clear){(void)clear;mac_sound_stop_all();memset(voices,0,sizeof voices);static_count=0;for(int i=0;i<2;i++){static_selected[i]=-1;ambient_running[i]=false;ambient_gain[i]=0;}}
void S_Shutdown(void){S_StopAllSounds(true);sound_started=0;}
void S_ClearBuffer(void){S_StopAllSounds(true);}
static float spatial(const vec3_t origin,float vol,float attenuation){
    float d=0;for(int i=0;i<3;i++){float x=origin[i]-listener[i];d+=x*x;}
    float gain=vol*volume*(1-sqrtf(d)*attenuation/1000);return gain<0?0:gain>1?1:gain;
}
static void start_loop(unsigned slot,sfx_t *sfx){
    char path[MAX_QPATH];unsigned bytes;qsound_t sound;
    snprintf(path,sizeof path,"sound/%s",sfx->name);byte *data=COM_LoadFileFromExtMem(path,&bytes);
    if(data && qsound_open(&sound,data,bytes))mac_sound_start(slot,&sound,0,0);
}
void S_StaticSound(int index,vec3_t origin,float vol,float attenuation){
    if(index<=0 || index>num_sfx || known_sfx[index-1].loopstart<0)return;
    if(static_count==STATIC_LIMIT){Con_Printf("Static sound limit reached\n");return;}
    unsigned i=static_count++;static_sounds[i].sfx=known_sfx+index-1;memcpy(static_sounds[i].origin,origin,sizeof(vec3_t));static_sounds[i].vol=vol/255;static_sounds[i].attenuation=attenuation/64;
}
void S_Update(vec3_t origin,vec3_t forward,vec3_t right,vec3_t up){
    (void)forward;(void)right;(void)up;memcpy(listener,origin,sizeof listener);
    double now=mac_time();
    for(unsigned i=0;i<DYNAMIC_VOICES;i++)if(voices[i].until>now){
        float g=voices[i].entity==_g->cl.viewentity?voices[i].vol*volume:spatial(voices[i].origin,voices[i].vol,voices[i].attenuation);
        if(g<0)g=0;if(g>1)g=1;qservice_sound_gain(i,g*255,g*255);
    }
    int best[2]={-1,-1};float gains[2]={0,0};
    for(unsigned i=0;i<static_count;i++){
        float g=spatial(static_sounds[i].origin,static_sounds[i].vol,static_sounds[i].attenuation);
        if(g>gains[0]){gains[1]=gains[0];best[1]=best[0];gains[0]=g;best[0]=i;}
        else if(g>gains[1]){gains[1]=g;best[1]=i;}
    }
    for(unsigned i=0;i<2;i++){
        if(best[i]!=static_selected[i]){mac_sound_stop(DYNAMIC_VOICES+i);if(best[i]>=0)start_loop(DYNAMIC_VOICES+i,static_sounds[best[i]].sfx);static_selected[i]=best[i];}
        qservice_sound_gain(DYNAMIC_VOICES+i,gains[i]*255,gains[i]*255);
    }
    mleaf_t *leaf=_g->cl.worldmodel && _g->cls.signon==4?Mod_PointInLeaf(origin,_g->cl.worldmodel):NULL;
    for(unsigned i=0;i<2;i++){
        float target=leaf?ambient_level*leaf->ambient_sound_level[i]:0;if(target<8)target=0;
        float step=host_frametime*ambient_fade;
        if(ambient_gain[i]<target){ambient_gain[i]+=step;if(ambient_gain[i]>target)ambient_gain[i]=target;}
        else{ambient_gain[i]-=step;if(ambient_gain[i]<target)ambient_gain[i]=target;}
        if(!ambient_running[i] && ambient_sfx[i]){start_loop(AMBIENT_FIRST+i,ambient_sfx[i]);ambient_running[i]=true;}
        float g=ambient_gain[i]*volume;if(g<0)g=0;if(g>255)g=255;qservice_sound_gain(AMBIENT_FIRST+i,g,g);
    }
}
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
