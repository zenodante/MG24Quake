/* Mac XIP/service harness with the original MG24 world renderer.
 * Host_Frame, server entities and gameplay are not connected yet. */
#include "service_sdl.h"
#include "qrender.h"
#include "qcollision.h"
#ifdef QMAC_MG24_RENDERER
#include "mg24_renderer.h"
#endif
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>
#include "resource_package.h"
#ifdef __APPLE__
#include <mach/mach.h>
#endif

typedef struct {
    qpak_t pak;
    qbsp_t world;
#ifndef QMAC_MG24_RENDERER
    qr_renderer_t renderer;
#endif
    qc_workspace_t collision;
    qr_camera_t camera;
    qsound_t sound;
    const uint8_t *palette;
    const char *map;
    unsigned frames,cycle,loaded,rendered,collision_queries;
    unsigned max_faces,max_leaves,max_nodes;
    uint64_t pixels,rejected;
    double render_seconds;
    bool scripted,uncapped;
    size_t peak_binding,package_bytes;
} game_t;
static game_t game;
static const char *maps[]={"maps/start.bsp","maps/e1m1.bsp","maps/e1m2.bsp","maps/e1m3.bsp","maps/e1m4.bsp","maps/e1m5.bsp","maps/e1m6.bsp","maps/e1m7.bsp","maps/e1m8.bsp"};
static const uint8_t *file(const char *name,size_t *bytes){
    qpak_file_t f;if(!qpak_find(&game.pak,name,&f))return NULL;
    if(bytes)*bytes=f.size;return qpak_map(&game.pak,&f,0,f.size);
}
#ifdef QMAC_MG24_RENDERER
#define RENDER_BIND(g) mg24_bind(&(g)->world)
#else
#define RENDER_BIND(g) qr_init(&(g)->renderer,&(g)->world)
#endif
static bool load_level(game_t *g,const char *name){
    mac_stack_sample();
    if(!qbsp_open(&g->world,&g->pak,name,NULL) || !RENDER_BIND(g) || !qr_spawn_camera(&g->world,&g->camera)){
        fprintf(stderr,"Cannot bind/spawn %s\n",name);return false;
    }
    unsigned counts[]={g->world.lump[QBSP_FACES].count,g->world.lump[QBSP_LEAVES].count,g->world.lump[QBSP_NODES].count};
    if(counts[0]>g->max_faces)g->max_faces=counts[0];if(counts[1]>g->max_leaves)g->max_leaves=counts[1];if(counts[2]>g->max_nodes)g->max_nodes=counts[2];
#ifdef QMAC_MG24_RENDERER
    if(mg24_binding_bytes()>g->peak_binding)g->peak_binding=mg24_binding_bytes();
#endif
    ++g->loaded;
    printf("bind %s: faces=%u leaves=%u nodes=%u immutable_payload=%u native_binding_accounted_in_report\n",name,counts[0],counts[1],counts[2],g->world.file.size);
    return true;
}
static int run_game(void *arg){
    game_t *g=arg;unsigned level=0;
    if(!load_level(g,g->map))return 1;
    size_t n;g->palette=file("gfx/palette.lmp",&n);if(!g->palette || n<768)return 1;
    const uint8_t *sound=file("sound/weapons/shotgn2.wav",&n);
    if(!sound || !qsound_open(&g->sound,sound,n) || g->sound.format!=QSOUND_QAD1){fprintf(stderr,"Missing QAD1 shotgun\n");return 1;}
    mac_sound_start(0,&g->sound,200,200);
    for(unsigned frame=0;!mac_quit() && (!g->frames || frame<g->frames);++frame){
        double begin=mac_time();unsigned pressed=mac_pressed(),buttons=mac_buttons();
        if((pressed&BTN_NEXT) || (g->cycle && frame && !(frame%g->cycle))){
            level=(level+1)%9;if(!load_level(g,maps[level]))return 1;
            mac_sound_start(0,&g->sound,200,200);
        }
        if(pressed&BTN_SOUND)mac_sound_start(0,&g->sound,200,200);
        float turn=(buttons&BTN_LEFT?1:0)-(buttons&BTN_RIGHT?1:0);
        g->camera.yaw+=turn*2;
        if(g->scripted)g->camera.yaw+=0.5f;
        float angle=g->camera.yaw*0.01745329252f;
        float forward=(buttons&BTN_FORWARD?1:0)-(buttons&BTN_BACK?1:0);
        float strafe=(buttons&BTN_STRAFE_RIGHT?1:0)-(buttons&BTN_STRAFE_LEFT?1:0);
        float move[3]={(forward*cosf(angle)+strafe*sinf(angle))*3,(forward*sinf(angle)-strafe*cosf(angle))*3,
                      ((buttons&BTN_UP?1:0)-(buttons&BTN_DOWN?1:0))*3.0f};
        float origin[3]={g->camera.position[0],g->camera.position[1],g->camera.position[2]-22};
        // No gravity/game entities yet: controlled collision-camera harness.
        if(qc_slide(&g->collision,&g->world,origin,move)){
            memcpy(g->camera.position,origin,sizeof origin);g->camera.position[2]+=22;
        }
        ++g->collision_queries;
        uint8_t *pixels=mac_frame_acquire();if(!pixels)break;
        memset(pixels+QR_WIDTH*QR_HEIGHT,0,MAC_FRAME_BYTES-QR_WIDTH*QR_HEIGHT);
        double draw_start=mac_time();
        #ifdef QMAC_MG24_RENDERER
        bool ok=mg24_draw(&g->camera,pixels);
#else
        bool ok=qr_draw(&g->renderer,&g->camera,pixels);
        g->pixels+=g->renderer.stats.pixels;g->rejected+=g->renderer.stats.rejected;
#endif
        mac_stack_sample();
        g->render_seconds+=mac_time()-draw_start;
        // A tiny progress strip uses the existing framebuffer, no UI surface.
        for(unsigned x=0;x<MAC_WIDTH;++x)pixels[192*MAC_WIDTH+x]=x<(frame%320)?250:16;
        mac_frame_submit(g->palette);++g->rendered;
        if(!ok){fprintf(stderr,"Renderer capacity exceeded\n");return 1;}
        double remaining=1.0/60.0-(mac_time()-begin);if(!g->uncapped && remaining>0)mac_delay((unsigned)(remaining*1000));
    }
    return 0;
}
static size_t resident(void){
#ifdef __APPLE__
    mach_task_basic_info_data_t info;mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
    if(task_info(mach_task_self(),MACH_TASK_BASIC_INFO,(task_info_t)&info,&count)==KERN_SUCCESS)return (size_t)info.resident_size;
#endif
    return 0;
}
static void report(FILE *f,const mac_stats_t *s,size_t mapped){
#ifdef QMAC_MG24_RENDERER
    size_t renderer_bytes=mg24_workspace_bytes(),binding_bytes=mg24_binding_bytes();
#else
    size_t renderer_bytes=sizeof game.renderer,binding_bytes=0;
#endif
    fprintf(f,"{\n  \"renderer\": \"%s\",\n  \"native_binding_bytes\": %zu,\n",
#ifdef QMAC_MG24_RENDERER
        "mg24_edge_surface_span_c",
#else
        "reference_triangles",
#endif
        binding_bytes);
#ifdef QMAC_MG24_RENDERER
    fprintf(f,"  \"simulated_flash_native_bytes\": %zu,\n  \"startup_pointer_relocations\": %zu,\n  \"level_metadata_heap_bytes\": 0,\n",mg24_image_bytes(),mg24_relocations());
#endif
    fprintf(f,"  \"resource_package_bytes\": %zu,\n",game.package_bytes);
    struct rusage usage;getrusage(RUSAGE_SELF,&usage);
    fprintf(f,"  \"mode\": \"xip_service_world_harness\",\n  \"pointer_bytes\": %zu,\n  \"frames\": %u,\n  \"maps_bound\": %u,\n  \"frame_hash\": \"%016llx\",\n  \"audio_hash\": \"%016llx\",\n",sizeof(void*),s->frames,game.loaded,(unsigned long long)s->frame_hash,(unsigned long long)s->audio_hash);
    fprintf(f,"  \"xip_readonly_mapped_bytes\": %zu,\n  \"native_binding_peak_bytes\": %zu,\n  \"flash_write_calls\": 0,\n  \"compressed_runtime_pointer_count\": 0,\n",mapped,game.peak_binding);
    fprintf(f,"  \"tracked_application_static_bytes\": %zu,\n  \"game_state_bytes\": %zu,\n  \"tracked_renderer_workspace_bytes\": %zu,\n  \"depth_bytes\": %zu,\n  \"collision_workspace_bytes\": %zu,\n  \"level_view_bytes\": %zu,\n",sizeof game+s->service_bytes
#ifdef QMAC_MG24_RENDERER
        +renderer_bytes
#endif
        ,sizeof game,renderer_bytes,(size_t)(QR_WIDTH*QR_HEIGHT*2),sizeof game.collision,sizeof game.world);
    fprintf(f,"  \"service_state_bytes\": %zu,\n  \"indexed_framebuffer_bytes\": %zu,\n  \"rgb565_line_buffers_bytes\": %zu,\n  \"palette_bytes\": %zu,\n  \"mixer_bytes\": %zu,\n  \"audio_scratch_bytes\": %zu,\n",s->service_bytes,s->frame_bytes,s->line_bytes,s->palette_bytes,s->mixer_bytes,s->audio_scratch_bytes);
    fprintf(f,"  \"worker_stack_reserved_bytes\": %zu,\n  \"worker_stack_sampled_bytes\": %zu,\n  \"audio_blocks\": %u,\n  \"audio_underruns\": %u,\n  \"audio_queue_peak_bytes\": %u,\n  \"adpcm_source_samples_estimate\": %llu,\n",s->worker_stack_reserved,s->worker_stack_observed,s->audio_blocks,s->audio_underruns,s->max_queued_audio_bytes,(unsigned long long)s->adpcm_decoded_samples);
    fprintf(f,"  \"worker_stack_pattern_highwater_bytes\": %zu,\n",s->worker_stack_highwater);
    fprintf(f,"  \"max_faces\": %u,\n  \"max_leaves\": %u,\n  \"max_nodes\": %u,\n  \"collision_queries\": %u,\n  \"rejected_faces\": %llu,\n  \"render_ms_mean\": %.4f,\n",game.max_faces,game.max_leaves,game.max_nodes,game.collision_queries,(unsigned long long)game.rejected,game.rendered?1000*game.render_seconds/game.rendered:0);
    fprintf(f,"  \"host_rss_after_shutdown_bytes\": %zu,\n  \"host_peak_rss_bytes\": %ld,\n  \"static_budget_excludes_unlisted_engine_globals\": true,\n  \"sdl_memory_excluded_from_application_budget\": true,\n  \"full_mg24_engine_running\": false,\n  \"failed\": %s\n}\n",resident(),usage.ru_maxrss,s->failed?"true":"false");
}
int main(int argc,char **argv){
    const char *assets=NULL,*output=NULL,*capture=NULL,*native=NULL;bool headless=false;game.map=maps[0];
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--headless"))headless=true;
        else if(!strcmp(argv[i],"--uncapped"))game.uncapped=true;
        else if(!strcmp(argv[i],"--scripted"))game.scripted=true;
        else if(i+1<argc && !strcmp(argv[i],"--assets"))assets=argv[++i];
        else if(i+1<argc && !strcmp(argv[i],"--native"))native=argv[++i];
        else if(i+1<argc && !strcmp(argv[i],"--map"))game.map=argv[++i];
        else if(i+1<argc && !strcmp(argv[i],"--report"))output=argv[++i];
        else if(i+1<argc && !strcmp(argv[i],"--capture"))capture=argv[++i];
        else if(i+1<argc && !strcmp(argv[i],"--frames"))game.frames=(unsigned)strtoul(argv[++i],NULL,10);
        else if(i+1<argc && !strcmp(argv[i],"--cycle"))game.cycle=(unsigned)strtoul(argv[++i],NULL,10);
        else{fprintf(stderr,"Unknown/incomplete option %s\n",argv[i]);return 2;}
    }
    if(!assets){fprintf(stderr,"Usage: quake_mac --assets quake-resources.qres [--frames N --headless --scripted --cycle N --report memory.json --capture frame.bmp]\n");return 2;}
    int fd=open(assets,O_RDONLY);struct stat st;
    if(fd<0 || fstat(fd,&st) || st.st_size<=0){perror(assets);if(fd>=0)close(fd);return 1;}
    size_t size=(size_t)st.st_size;void *image=mmap(NULL,size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);
    if(image==MAP_FAILED){perror("mmap");return 1;}
    uint32_t qo=0,qs=0,no=0,ns=0;
    bool packaged=size>=4 && !memcmp(image,"QRES",4);
    if(packaged && (native || !qres_open(image,size,&qo,&qs,&no,&ns))){
        fprintf(stderr,"Invalid QRES or conflicting --native option\n");munmap(image,size);return 1;
    }
    if(packaged)game.package_bytes=size;
    if(!qpak_open(&game.pak,(uint8_t*)image+qo,packaged?qs:size) || game.pak.xip_version!=3){fprintf(stderr,"Requires validated QXIP3/QLV1 image\n");munmap(image,size);return 1;}
#ifdef QMAC_MG24_RENDERER
    if(!(packaged?mg24_image_open_range(&game.pak,assets,no,ns):(native && mg24_image_open(&game.pak,native)))){
        fprintf(stderr,"Requires matching offline QNAT image: --native image.qnat\n");munmap(image,size);return 1;
    }
#else
    (void)native;
#endif
    mac_stats_t stats={0};int result=mac_run(run_game,&game,headless,capture,&stats);
    report(stdout,&stats,game.pak.bytes);
    if(output){FILE *f=fopen(output,"w");if(!f){perror(output);result=1;}else{report(f,&stats,game.pak.bytes);if(fclose(f))result=1;}}
#ifdef QMAC_MG24_RENDERER
    mg24_shutdown();
#endif
    munmap(image,size);return result;
}
