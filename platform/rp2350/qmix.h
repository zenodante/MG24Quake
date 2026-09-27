#ifndef QMIX_H
#define QMIX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { QMIX_CHANNELS=8, QMIX_RATE=22050, QMIX_BLOCK=128, QSOUND_QAD1_BLOCK_SAMPLES=256 };
typedef enum { QSOUND_PCM=0, QSOUND_QAD1=1 } qsound_format_t;
typedef struct {
    const uint8_t *data;
    size_t bytes;
    uint32_t length, rate;
    int32_t loop;
    uint16_t block_samples;
    uint8_t width;
    qsound_format_t format;
} qsound_t;
typedef struct {
    qsound_t sound;
    uint32_t position, phase;
    uint16_t left, right;
    int32_t adpcm_predictor;
    uint32_t adpcm_position;
    uint8_t adpcm_index;
    bool adpcm_valid;
    bool active;
} qmix_channel_t;
typedef struct { qmix_channel_t channels[QMIX_CHANNELS]; } qmix_t;
/* Parsers only retain pointers into immutable XIP data; no allocation occurs. */
bool qsound_wav(qsound_t *sound,const uint8_t *wav,size_t bytes);
bool qsound_qad1(qsound_t *sound,const uint8_t *data,size_t bytes);
bool qsound_open(qsound_t *sound,const uint8_t *data,size_t bytes);
void qmix_start(qmix_t *m,unsigned channel,const qsound_t *sound,unsigned left,unsigned right);
void qmix_stop(qmix_t *m,unsigned channel);
void qmix_render(qmix_t *m,uint16_t *pwm,size_t samples);
#endif
