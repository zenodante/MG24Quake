#ifndef QMAC_SERVICE_H
#define QMAC_SERVICE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qmix.h"
enum { MAC_WIDTH=320,MAC_HEIGHT=200,MAC_FRAME_BYTES=64000 };
enum { BTN_FORWARD=1,BTN_BACK=2,BTN_LEFT=4,BTN_RIGHT=8,BTN_UP=16,BTN_DOWN=32,
       BTN_SOUND=64,BTN_NEXT=128,BTN_STRAFE_LEFT=256,BTN_STRAFE_RIGHT=512 };
typedef struct {
    unsigned frames,audio_blocks,audio_underruns;
    unsigned max_queued_audio_bytes;
    uint64_t frame_hash,audio_hash,adpcm_decoded_samples;
    size_t service_bytes,frame_bytes,line_bytes,palette_bytes,mixer_bytes,audio_scratch_bytes;
    size_t worker_stack_reserved,worker_stack_observed,worker_stack_highwater;
    bool failed;
} mac_stats_t;
typedef int (*mac_game_fn)(void *);
/* SDL main thread acts as Core 1. Game callback is the sole Core-0 worker. */
int mac_run(mac_game_fn game,void *arg,bool headless,const char *capture,mac_stats_t *stats);
uint8_t *mac_frame_acquire(void);
bool mac_frame_submit(const uint8_t palette[768]);
bool mac_quit(void);
void mac_request_quit(void);
void mac_finish(bool failed);
void mac_game_input(bool enabled);
bool mac_key_event(int *key,bool *down);
uint32_t mac_buttons(void);
uint32_t mac_pressed(void);
bool mac_sound_start(unsigned channel,const qsound_t *sound,unsigned left,unsigned right);
void mac_sound_stop(unsigned channel);
void mac_sound_stop_all(void);
void mac_stack_sample(void);
void mac_delay(unsigned milliseconds);
double mac_time(void);
#endif
