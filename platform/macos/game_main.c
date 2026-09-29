/* Full MG24 Host_Frame entry; core-1 SDL service owns presentation and audio. */
#include "quakedef.h"
#include "service_sdl.h"
#include <sys/resource.h>
static unsigned frame_limit,cycle_frames;
static bool scripted,uncapped;
static const char *initial_map="start";
const char *qmac_game_pak;
static unsigned executed_frames,peak_entities;
static size_t peak_zone,peak_legacy;
static unsigned signed_frames[9];
extern size_t qmac_legacy_flash_used(void),qmac_legacy_flash_reserved(void);
extern void game_resource_report(FILE *f);
static const char *maps[]={"start","e1m1","e1m2","e1m3","e1m4","e1m5","e1m6","e1m7","e1m8"};
static void map_command(const char *map){char command[80];snprintf(command,sizeof(command),"map %s\n",map);Cbuf_AddText(command);}
static int run_engine(void *unused){
    (void)unused;memset(_g,0,sizeof(*_g));Z_Init();Cvar_Init();Host_Init(NULL);
    Cbuf_AddText("bind w +forward\nbind s +back\nbind a +moveleft\nbind d +moveright\nbind SPACE +jump\nbind CTRL +attack\n");
    map_command(initial_map);
    unsigned level=0;
    for(unsigned i=0;!mac_quit() && (!frame_limit || i<frame_limit);i++){
        double begin=mac_time();
        if(cycle_frames && i && i%cycle_frames==0)map_command(maps[++level%9]);
        unsigned phase=cycle_frames?i%cycle_frames:i;
        if(scripted){if(phase==120)Cbuf_AddText("+attack\n+forward\n");if(phase==240)Cbuf_AddText("-forward\n-attack\n");}
        Host_Frame(1.0f/60.0f);mac_stack_sample();executed_frames++;
        for(unsigned m=0;m<9;m++)if(sv.active && _g->cls.signon==4 && !strcmp(sv.name,maps[m]))signed_frames[m]++;
        size_t legacy=qmac_legacy_flash_used();if(legacy>peak_legacy)peak_legacy=legacy;
        if(sv.num_edicts>peak_entities)peak_entities=sv.num_edicts;
        size_t used=MAX_STATIC_ZONE-getZoneRemainingSize();if(used>peak_zone)peak_zone=used;
        double remain=1.0/60-(mac_time()-begin);if(!uncapped && remain>0)mac_delay((unsigned)(remain*1000));
    }
    printf("GAME server=%d signon=%d entities=%d health=%d shells=%d time=%.3f\n",sv.active,_g->cls.signon,sv.num_edicts,_g->cl.stats[STAT_HEALTH],_g->cl.stats[STAT_SHELLS],sv.time);
    return 0;
}
int main(int argc,char **argv){
    const char *capture=NULL,*report=NULL;bool headless=false;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--headless"))headless=true;
        else if(!strcmp(argv[i],"--scripted"))scripted=true;
        else if(!strcmp(argv[i],"--uncapped"))uncapped=true;
        else if(i+1<argc && !strcmp(argv[i],"--assets"))qmac_game_pak=argv[++i];
        else if(i+1<argc && !strcmp(argv[i],"--map"))initial_map=argv[++i];
        else if(i+1<argc && !strcmp(argv[i],"--frames"))frame_limit=strtoul(argv[++i],NULL,10);
        else if(i+1<argc && !strcmp(argv[i],"--cycle"))cycle_frames=strtoul(argv[++i],NULL,10);
        else if(i+1<argc && !strcmp(argv[i],"--capture"))capture=argv[++i];
        else if(i+1<argc && !strcmp(argv[i],"--report"))report=argv[++i];
        else {fprintf(stderr,"Unknown/incomplete argument: %s\n",argv[i]);return 2;}
    }
    if(!qmac_game_pak){fprintf(stderr,"usage: quake_game --assets game.qres [--map e1m1 --frames 600 --headless --scripted --report game.json]\n");return 2;}
    if(strlen(initial_map)>32 || strspn(initial_map,"abcdefghijklmnopqrstuvwxyz0123456789_")!=strlen(initial_map)){fprintf(stderr,"Invalid map name\n");return 2;}
    setvbuf(stdout,NULL,_IOLBF,0);mac_game_input(true);
    mac_stats_t stats={0};int result=mac_run(run_engine,NULL,headless,capture,&stats);
    if(report){
        FILE *f=fopen(report,"w");if(!f){perror(report);return 1;}
        struct rusage usage;getrusage(RUSAGE_SELF,&usage);
        extern unsigned game_sound_starts;
        extern size_t qmac_zone_peak;
        fprintf(f,"{\n  \"memory_scope\": \"Selected host allocations, not a complete MCU SRAM budget\",\n  \"global_state_bytes\": %zu,\n  \"zbuffer_bytes\": %zu,\n  \"legacy_generated_frame_peak_bytes\": %zu,\n  \"host_legacy_internal_flash_reserved_bytes\": %zu,\n  \"host_legacy_external_flash_reserved_bytes\": %u,\n",sizeof(*_g),(size_t)_3D_VIEWPORT_SIZE*2,peak_legacy,qmac_legacy_flash_reserved(),EXT_MEMORY_SIZE);
        game_resource_report(f);
#if QMAC_MEMORY_AUDIT
        extern void qmac_memory_report(FILE*);qmac_memory_report(f);
#endif
        fprintf(f,"  \"signed_on_frames_by_map\": {");
        for(unsigned m=0;m<9;m++)fprintf(f,"%s\"%s\": %u",m?", ":"",maps[m],signed_frames[m]);
        fprintf(f,"},\n  \"full_engine_host_frame\": true,\n  \"frames\": %u,\n  \"presented_frames\": %u,\n  \"server_active\": %s,\n  \"signon\": %d,\n  \"peak_entities\": %u,\n  \"health\": %d,\n  \"shells\": %d,\n  \"zone_capacity_bytes\": %u,\n  \"zone_frame_peak_bytes\": %zu,\n  \"zone_allocation_peak_bytes\": %zu,\n  \"service_bytes\": %zu,\n  \"framebuffer_bytes\": %zu,\n  \"line_buffers_bytes\": %zu,\n  \"stack_highwater_bytes\": %zu,\n  \"sound_starts\": %u,\n  \"adpcm_samples\": %llu,\n  \"frame_hash\": \"%016llx\",\n  \"host_peak_rss_bytes\": %ld,\n  \"failed\": %s\n}\n",executed_frames,stats.frames,sv.active?"true":"false",_g->cls.signon,peak_entities,_g->cl.stats[STAT_HEALTH],_g->cl.stats[STAT_SHELLS],MAX_STATIC_ZONE,peak_zone,qmac_zone_peak,stats.service_bytes,stats.frame_bytes,stats.line_bytes,stats.worker_stack_highwater,game_sound_starts,(unsigned long long)stats.adpcm_decoded_samples,(unsigned long long)stats.frame_hash,usage.ru_maxrss,result?"true":"false");
        fclose(f);
    }
    return result;
}
