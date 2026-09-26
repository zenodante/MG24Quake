#ifndef QMIX_H
#define QMIX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { QMIX_CHANNELS=8, QMIX_RATE=22050, QMIX_BLOCK=256 };
typedef struct {
    const uint8_t *pcm;
    uint32_t length, rate;
    int32_t loop;
    uint8_t width;
} qsound_t;
typedef struct {
    qsound_t sound;
    uint32_t position, phase;
    uint16_t left, right;
    bool active;
} qmix_channel_t;
typedef struct { qmix_channel_t channels[QMIX_CHANNELS]; } qmix_t;
/* Original PCM WAV in XIP. No allocation or mutable shared resource cursor. */
bool qsound_wav(qsound_t *sound,const uint8_t *wav,size_t bytes);
void qmix_start(qmix_t *m,unsigned channel,const qsound_t *sound,unsigned left,unsigned right);
void qmix_stop(qmix_t *m,unsigned channel);
void qmix_render(qmix_t *m,uint16_t *pwm,size_t samples);
#endif
