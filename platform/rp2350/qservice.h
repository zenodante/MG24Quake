#ifndef QSERVICE_H
#define QSERVICE_H
#include "qmix.h"
#include <stdbool.h>
#include <stdint.h>
enum { Q_WIDTH=320,Q_HEIGHT=200,Q_FRAME_BYTES=64000 };
typedef struct { uint32_t timestamp; uint16_t held; } qinput_event_t;
typedef struct { uint32_t frames,audio_blocks,audio_underruns,input_errors,input_overflows; } qservice_stats_t;
/* Core 0 API. All output/input peripheral calls execute on core 1. */
bool qservice_start(void);
uint8_t *qservice_frame_acquire(unsigned *slot);
void qservice_frame_submit(unsigned slot,const uint8_t palette[768]);
bool qservice_input_pop(qinput_event_t *event);
uint16_t qservice_buttons(void);
/* false means queue full: caller must retry; commands are never silently lost.
 * Sound PCM pointers must remain valid until a stop/fence acknowledges them. */
bool qservice_sound_start(unsigned channel,const qsound_t *sound,unsigned left,unsigned right);
bool qservice_sound_stop(unsigned channel);
bool qservice_sound_stop_all(uint32_t fence);
bool qservice_sound_fence_done(uint32_t fence);
void qservice_stats(qservice_stats_t *stats);
#endif
