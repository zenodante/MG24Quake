/* SDL platform follows pico8c/platform/sdl's RGB565 row uploads and queued
 * audio. macOS requires SDL video/events on main: main therefore models core 1.
 * The game worker is core 0. SDL's audio backend consumes queued PCM only;
 * all QAD1 decoding and mixing runs on the service thread. */
#include "service_sdl.h"
#include <SDL.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include <unistd.h>

enum { FRAME_FREE,FRAME_DRAWING,FRAME_READY,FRAME_READING, FRAME_DONE };
typedef struct {
    uint8_t frame[MAC_FRAME_BYTES];
    uint16_t lines[2][MAC_WIDTH];
    uint16_t palette[256];
    uint16_t audio_pwm[QMIX_BLOCK];
    int16_t audio_pcm[QMIX_BLOCK];
    qmix_t mixer;
    pthread_mutex_t lock;
    pthread_cond_t changed;
    unsigned owner;
    bool done;
    atomic_bool failed;
    atomic_bool quit;
    atomic_uint buttons,pressed;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_AudioDeviceID audio;
    mac_game_fn game;
    void *game_arg;
    uintptr_t stack_top;
    struct {int key;bool down;} keys[128];
    unsigned key_read,key_write;
    mac_stats_t stats;
} mac_service_t;
static mac_service_t s;
static bool game_input_enabled;
void mac_game_input(bool enabled){game_input_enabled=enabled;}
bool mac_key_event(int *key,bool *down){
    pthread_mutex_lock(&s.lock);bool present=s.key_read!=s.key_write;
    if(present){*key=s.keys[s.key_read].key;*down=s.keys[s.key_read].down;s.key_read=(s.key_read+1)%128;}
    pthread_mutex_unlock(&s.lock);return present;
}
static pthread_t main_thread,game_thread;
static uint64_t hash_bytes(uint64_t h,const void *p,size_t bytes){
    const uint8_t *b=p;
    if(!h)h=UINT64_C(14695981039346656037);
    while(bytes--){h^=*b++;h*=UINT64_C(1099511628211);}return h;
}
double mac_time(void){return (double)SDL_GetPerformanceCounter()/SDL_GetPerformanceFrequency();}
void mac_delay(unsigned ms){SDL_Delay(ms);}
void mac_request_quit(void){
    atomic_store(&s.quit,true);pthread_mutex_lock(&s.lock);pthread_cond_broadcast(&s.changed);pthread_mutex_unlock(&s.lock);
}
bool mac_quit(void){return atomic_load_explicit(&s.quit,memory_order_acquire);}
uint32_t mac_buttons(void){return atomic_load_explicit(&s.buttons,memory_order_relaxed);}
uint32_t mac_pressed(void){return atomic_exchange_explicit(&s.pressed,0,memory_order_relaxed);}
void mac_stack_sample(void){
    volatile unsigned marker=0;
    uintptr_t p=(uintptr_t)&marker;
    size_t used=s.stack_top>p?s.stack_top-p:p-s.stack_top;
    if(used>s.stats.worker_stack_observed)s.stats.worker_stack_observed=used;
}
uint8_t *mac_frame_acquire(void){
    assert(pthread_equal(pthread_self(),game_thread));
    pthread_mutex_lock(&s.lock);
    while(s.owner!=FRAME_FREE && !mac_quit())pthread_cond_wait(&s.changed,&s.lock);
    uint8_t *p=NULL;
    if(!mac_quit()){s.owner=FRAME_DRAWING;p=s.frame;}
    pthread_mutex_unlock(&s.lock);return p;
}
bool mac_frame_submit(const uint8_t palette[768]){
    assert(pthread_equal(pthread_self(),game_thread));
    pthread_mutex_lock(&s.lock);
    assert(s.owner==FRAME_DRAWING);
    for(unsigned i=0;i<256;++i)s.palette[i]=(uint16_t)((palette[i*3]>>3)<<11 | (palette[i*3+1]>>2)<<5 | palette[i*3+2]>>3);
    s.owner=FRAME_READY;pthread_cond_broadcast(&s.changed);
    pthread_mutex_unlock(&s.lock);return !mac_quit();
}
bool mac_sound_start(unsigned channel,const qsound_t *sound,unsigned left,unsigned right){
    if(channel>=QMIX_CHANNELS || !sound)return false;
    pthread_mutex_lock(&s.lock);qmix_start(&s.mixer,channel,sound,left,right);pthread_mutex_unlock(&s.lock);return true;
}
void mac_sound_stop(unsigned channel){pthread_mutex_lock(&s.lock);qmix_stop(&s.mixer,channel);pthread_mutex_unlock(&s.lock);}
void mac_sound_stop_all(void){pthread_mutex_lock(&s.lock);memset(&s.mixer,0,sizeof s.mixer);pthread_mutex_unlock(&s.lock);}
void mac_finish(bool failed){
    pthread_mutex_lock(&s.lock);if(failed)atomic_store(&s.failed,true);s.done=true;pthread_cond_broadcast(&s.changed);pthread_mutex_unlock(&s.lock);
}
static void *worker(void *unused){
    (void)unused;
    game_thread=pthread_self();
#ifdef __APPLE__
    s.stack_top=(uintptr_t)pthread_get_stackaddr_np(game_thread);
    s.stats.worker_stack_reserved=pthread_get_stacksize_np(game_thread);
#else
    volatile unsigned marker=0;s.stack_top=(uintptr_t)&marker;
#endif
    int result=s.game(s.game_arg);mac_stack_sample();mac_finish(result!=0);return NULL;
}
static void input(void){
    SDL_Event event;
    while(SDL_PollEvent(&event)){
        if(event.type==SDL_QUIT || (!game_input_enabled && event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE))atomic_store(&s.quit,true);
        if(game_input_enabled && (event.type==SDL_KEYDOWN || event.type==SDL_KEYUP || event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP)){
            int key=(event.type==SDL_KEYDOWN || event.type==SDL_KEYUP)?event.key.keysym.sym:0x20000000+event.button.button;
            bool down=event.type==SDL_KEYDOWN || event.type==SDL_MOUSEBUTTONDOWN;
            pthread_mutex_lock(&s.lock);unsigned next=(s.key_write+1)%128;
            if(next!=s.key_read){s.keys[s.key_write].key=key;s.keys[s.key_write].down=down;s.key_write=next;}
            else {s.failed=true;atomic_store(&s.quit,true);}
            pthread_mutex_unlock(&s.lock);
        }
    }
    const uint8_t *k=SDL_GetKeyboardState(NULL);unsigned b=0;
    if(k[SDL_SCANCODE_W]||k[SDL_SCANCODE_UP])b|=BTN_FORWARD;
    if(k[SDL_SCANCODE_S]||k[SDL_SCANCODE_DOWN])b|=BTN_BACK;
    if(k[SDL_SCANCODE_LEFT])b|=BTN_LEFT;
    if(k[SDL_SCANCODE_RIGHT])b|=BTN_RIGHT;
    if(k[SDL_SCANCODE_E])b|=BTN_UP;
    if(k[SDL_SCANCODE_Q])b|=BTN_DOWN;
    if(k[SDL_SCANCODE_A])b|=BTN_STRAFE_LEFT;
    if(k[SDL_SCANCODE_D])b|=BTN_STRAFE_RIGHT;
    if(k[SDL_SCANCODE_SPACE])b|=BTN_SOUND;
    if(k[SDL_SCANCODE_TAB])b|=BTN_NEXT;
    unsigned old=atomic_exchange(&s.buttons,b);atomic_fetch_or(&s.pressed,b&~old);
}
static void audio_tick(void){
    assert(pthread_equal(pthread_self(),main_thread));
    unsigned queued=SDL_GetQueuedAudioSize(s.audio);
    if(s.stats.audio_blocks && !queued)++s.stats.audio_underruns;
    while(queued<QMIX_BLOCK*2*3){
        pthread_mutex_lock(&s.lock);
        for(unsigned i=0;i<QMIX_CHANNELS;++i)if(s.mixer.channels[i].active && s.mixer.channels[i].sound.format==QSOUND_QAD1)
            s.stats.adpcm_decoded_samples+=QMIX_BLOCK/2;
        qmix_render(&s.mixer,s.audio_pwm,QMIX_BLOCK);
        pthread_mutex_unlock(&s.lock);
        for(unsigned i=0;i<QMIX_BLOCK;++i)s.audio_pcm[i]=(int16_t)(((int)s.audio_pwm[i]-128)*256);
        s.stats.audio_hash=hash_bytes(s.stats.audio_hash,s.audio_pcm,sizeof s.audio_pcm);
        if(SDL_QueueAudio(s.audio,s.audio_pcm,sizeof s.audio_pcm)){s.failed=true;atomic_store(&s.quit,true);break;}
        ++s.stats.audio_blocks;queued+=sizeof s.audio_pcm;
    }
    if(queued>s.stats.max_queued_audio_bytes)s.stats.max_queued_audio_bytes=queued;
}
static bool draw_frame(void){
    assert(pthread_equal(pthread_self(),main_thread));
    pthread_mutex_lock(&s.lock);
    bool ready=s.owner==FRAME_READY;
    if(ready)s.owner=FRAME_READING;
    pthread_mutex_unlock(&s.lock);
    if(!ready)return false;
    s.stats.frame_hash=hash_bytes(s.stats.frame_hash,s.frame,sizeof s.frame);
    for(unsigned y=0;y<MAC_HEIGHT;++y){
        uint16_t *line=s.lines[y&1];
        for(unsigned x=0;x<MAC_WIDTH;++x)line[x]=s.palette[s.frame[y*MAC_WIDTH+x]];
        SDL_Rect row={0,(int)y,MAC_WIDTH,1};
        if(SDL_UpdateTexture(s.texture,&row,line,MAC_WIDTH*2)){s.failed=true;atomic_store(&s.quit,true);break;}
        // Match service scheduling: audio/input remain responsive during upload.
        if(!(y&15)){audio_tick();input();}
    }
    SDL_RenderClear(s.renderer);SDL_RenderCopy(s.renderer,s.texture,NULL,NULL);SDL_RenderPresent(s.renderer);
    ++s.stats.frames;
    pthread_mutex_lock(&s.lock);s.owner=FRAME_FREE;pthread_cond_broadcast(&s.changed);pthread_mutex_unlock(&s.lock);return true;
}
int mac_run(mac_game_fn game,void *arg,bool headless,const char *capture,mac_stats_t *stats){
    memset(&s,0,sizeof s);s.game=game;s.game_arg=arg;main_thread=pthread_self();
    pthread_mutex_init(&s.lock,NULL);pthread_cond_init(&s.changed,NULL);
    atomic_init(&s.failed,false);atomic_init(&s.quit,false);atomic_init(&s.buttons,0);atomic_init(&s.pressed,0);
    if(headless){SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);}
    int result=1;bool started=false;pthread_t thread;
    void *stack_mapping=MAP_FAILED;size_t stack_bytes=256*1024,stack_total=0,page=(size_t)sysconf(_SC_PAGESIZE);
    uint8_t *stack=NULL;
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS))goto shutdown;
    s.window=SDL_CreateWindow("Quake XIP / Core 0 + Core 1",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,960,600,0);
    if(!s.window)goto shutdown;
    s.renderer=SDL_CreateRenderer(s.window,-1,SDL_RENDERER_ACCELERATED);
    if(!s.renderer)s.renderer=SDL_CreateRenderer(s.window,-1,SDL_RENDERER_SOFTWARE);
    if(!s.renderer)goto shutdown;
    SDL_RenderSetLogicalSize(s.renderer,MAC_WIDTH,MAC_HEIGHT);
    s.texture=SDL_CreateTexture(s.renderer,SDL_PIXELFORMAT_RGB565,SDL_TEXTUREACCESS_STREAMING,MAC_WIDTH,MAC_HEIGHT);
    if(!s.texture)goto shutdown;
    // Warm up the SDL/GPU presentation path before starting real-time audio.
    memset(s.lines,0,sizeof s.lines);
    for(unsigned y=0;y<MAC_HEIGHT;++y){SDL_Rect row={0,(int)y,MAC_WIDTH,1};
        if(SDL_UpdateTexture(s.texture,&row,s.lines[y&1],MAC_WIDTH*2))goto shutdown;}
    SDL_RenderClear(s.renderer);SDL_RenderCopy(s.renderer,s.texture,NULL,NULL);SDL_RenderPresent(s.renderer);
    SDL_AudioSpec desired={0};desired.freq=QMIX_RATE;desired.channels=1;desired.format=AUDIO_S16SYS;desired.samples=QMIX_BLOCK;
    s.audio=SDL_OpenAudioDevice(NULL,0,&desired,NULL,0);if(!s.audio)goto shutdown;
    audio_tick();SDL_PauseAudioDevice(s.audio,0);
    stack_total=stack_bytes+2*page;
    stack_mapping=mmap(NULL,stack_total,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(stack_mapping==MAP_FAILED)goto shutdown;
    stack=(uint8_t*)stack_mapping+page;
    if(mprotect(stack,stack_bytes,PROT_READ|PROT_WRITE))goto shutdown;
    memset(stack,0xa5,stack_bytes);
    pthread_attr_t attr;pthread_attr_init(&attr);
    if(pthread_attr_setstack(&attr,stack,stack_bytes)){pthread_attr_destroy(&attr);goto shutdown;}
    int error=pthread_create(&thread,&attr,worker,NULL);pthread_attr_destroy(&attr);if(error)goto shutdown;
    started=true;
    for(;;){
        input();audio_tick();draw_frame();
        pthread_mutex_lock(&s.lock);
        bool done=s.done && s.owner!=FRAME_READY && s.owner!=FRAME_READING;
        if(mac_quit())pthread_cond_broadcast(&s.changed);
        pthread_mutex_unlock(&s.lock);
        if(done)break;
        SDL_Delay(1);
    }
    pthread_join(thread,NULL);started=false;
    // Optional screenshot allocates SDL-owned capture storage only after the
    // worker has stopped; it is excluded from the steady-state memory budget.
    if(capture){
        SDL_Surface *pic=SDL_CreateRGBSurfaceWithFormat(0,MAC_WIDTH,MAC_HEIGHT,16,SDL_PIXELFORMAT_RGB565);
        if(!pic)s.failed=true;
        else{
            for(unsigned y=0;y<MAC_HEIGHT;++y){uint16_t *row=(uint16_t*)((uint8_t*)pic->pixels+y*pic->pitch);
                for(unsigned x=0;x<MAC_WIDTH;++x)row[x]=s.palette[s.frame[y*MAC_WIDTH+x]];}
            if(SDL_SaveBMP(pic,capture))s.failed=true;SDL_FreeSurface(pic);
        }
    }
    size_t untouched=0;while(untouched<stack_bytes && stack[untouched]==0xa5)++untouched;
    s.stats.worker_stack_highwater=stack_bytes-untouched;
    s.stats.service_bytes=sizeof s+sizeof main_thread+sizeof game_thread;s.stats.frame_bytes=sizeof s.frame;s.stats.line_bytes=sizeof s.lines;
    s.stats.palette_bytes=sizeof s.palette;s.stats.mixer_bytes=sizeof s.mixer;
    s.stats.audio_scratch_bytes=sizeof s.audio_pwm+sizeof s.audio_pcm;
    s.stats.failed=s.failed;*stats=s.stats;result=s.failed?1:0;
shutdown:
    if(result && !s.failed)fprintf(stderr,"SDL: %s\n",SDL_GetError());
    if(started){atomic_store(&s.quit,true);pthread_mutex_lock(&s.lock);pthread_cond_broadcast(&s.changed);pthread_mutex_unlock(&s.lock);pthread_join(thread,NULL);}
    if(s.audio)SDL_CloseAudioDevice(s.audio);
    if(s.texture)SDL_DestroyTexture(s.texture);
    if(s.renderer)SDL_DestroyRenderer(s.renderer);
    if(s.window)SDL_DestroyWindow(s.window);
    if(stack_mapping!=MAP_FAILED)munmap(stack_mapping,stack_total);
    SDL_Quit();pthread_cond_destroy(&s.changed);pthread_mutex_destroy(&s.lock);return result;
}
