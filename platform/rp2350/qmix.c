#include "qmix.h"
#include <limits.h>
#include <string.h>

static const int16_t ima_step[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int8_t ima_index[8]={-1,-1,-1,-1,2,4,6,8};
static uint32_t rd32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned rd16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static int16_t rds16(const uint8_t *p){return (int16_t)rd16(p);}

bool qsound_wav(qsound_t *s,const uint8_t *wav,size_t bytes){
    if(!s||!wav||bytes<12||memcmp(wav,"RIFF",4)||memcmp(wav+8,"WAVE",4))return false;
    qsound_t out={.loop=-1,.format=QSOUND_PCM,.data=NULL,.bytes=bytes};bool format=false;uint32_t loop_length=0;
    for(size_t pos=12;pos<=bytes&&bytes-pos>=8;){
        const uint8_t *h=wav+pos,*d=h+8;uint32_t n=rd32(h+4);if(n>bytes-pos-8)break;
        if(!memcmp(h,"fmt ",4)){
            if(n<16||rd16(d)!=1||rd16(d+2)!=1||(rd16(d+14)!=8&&rd16(d+14)!=16))return false;
            out.width=(uint8_t)(rd16(d+14)/8);if(rd16(d+12)!=out.width)return false;
            out.rate=rd32(d+4);if(out.rate!=11025&&out.rate!=22050)return false;format=true;
        }else if(!memcmp(h,"data",4)){out.data=d;out.length=n;}
        else if(!memcmp(h,"cue ",4)&&n>=28&&rd32(d)>0)out.loop=(int32_t)rd32(d+24);
        else if(!memcmp(h,"LIST",4)&&n>=24&&!memcmp(d+20,"mark",4))loop_length=rd32(d+16);
        pos+=8+n+(n&1u);
    }
    if(!format||!out.data||!out.length||out.loop < -1||out.length%out.width)return false;
    out.length/=out.width;
    if(out.loop>=0){if((uint32_t)out.loop>=out.length)return false;if(loop_length){if(loop_length>out.length-(uint32_t)out.loop)return false;out.length=(uint32_t)out.loop+loop_length;}}
    *s=out;return true;
}

bool qsound_qad1(qsound_t *s,const uint8_t *data,size_t bytes){
    if(!s||!data||bytes<16||memcmp(data,"QAD1",4))return false;
    uint32_t length=rd32(data+4),raw_loop=rd32(data+8);uint16_t block_samples=(uint16_t)rd16(data+12),reserved=(uint16_t)rd16(data+14);
    if(!length||block_samples!=QSOUND_QAD1_BLOCK_SAMPLES||reserved||
       (raw_loop!=UINT32_MAX&&raw_loop>=length))return false;
    uint32_t blocks=(length+block_samples-1u)/block_samples;size_t pos=16;
    for(uint32_t b=0;b<blocks;++b){
        if(bytes-pos<6)return false;unsigned index=data[pos+2],flags=data[pos+3],count=rd16(data+pos+4);
        uint32_t expected=length-b*block_samples;if(expected>block_samples)expected=block_samples;
        if(index>88||flags||count!=expected||!count)return false;
        size_t packed=(count-1u+1u)/2u;if(packed>bytes-pos-6)return false;pos+=6+packed;
    }
    if(pos!=bytes)return false;
    *s=(qsound_t){.data=data,.bytes=bytes,.length=length,.rate=11025,
        .loop=raw_loop==UINT32_MAX?-1:(int32_t)raw_loop,.block_samples=block_samples,
        .width=0,.format=QSOUND_QAD1};return true;
}

bool qsound_open(qsound_t *s,const uint8_t *data,size_t bytes){
    if(bytes>=4&&!memcmp(data,"QAD1",4))return qsound_qad1(s,data,bytes);
    return qsound_wav(s,data,bytes);
}

static const uint8_t *qad_block(const qsound_t *s,uint32_t block,unsigned *count){
    size_t stride=6u+(s->block_samples-1u+1u)/2u;size_t off=16u+(size_t)block*stride;
    if(off+6>s->bytes)return NULL;unsigned n=rd16(s->data+off+4);size_t packed=(n-1u+1u)/2u;
    if(!n||off+6+packed>s->bytes)return NULL;*count=n;return s->data+off;
}
static int32_t ima_decode(uint8_t code,int32_t predictor,uint8_t *index){
    int step=ima_step[*index],delta=step>>3;if(code&4)delta+=step;if(code&2)delta+=step>>1;if(code&1)delta+=step>>2;
    predictor+=(code&8)?-delta:delta;if(predictor>32767)predictor=32767;else if(predictor<-32768)predictor=-32768;
    int next=(int)*index+ima_index[code&7];if(next<0)next=0;else if(next>88)next=88;*index=(uint8_t)next;return predictor;
}
static bool qad_seek(qmix_channel_t *c,uint32_t position){
    const qsound_t *s=&c->sound;uint32_t block=position/s->block_samples;unsigned count;const uint8_t *p=qad_block(s,block,&count);unsigned within=position%s->block_samples;
    if(!p||within>=count)return false;c->adpcm_predictor=rds16(p);c->adpcm_index=p[2];c->adpcm_position=block*s->block_samples;c->adpcm_valid=true;
    for(unsigned i=0;i<within;++i){uint8_t packed=p[6+i/2],code=(i&1)?packed>>4:packed&15;c->adpcm_predictor=ima_decode(code,c->adpcm_predictor,&c->adpcm_index);++c->adpcm_position;}
    return true;
}
static int channel_sample(qmix_channel_t *c){
    if(c->sound.format==QSOUND_PCM)return c->sound.width==1?(int)c->sound.data[c->position]-128:(int)rds16(c->sound.data+2*c->position)/256;
    if(!c->adpcm_valid||c->adpcm_position!=c->position){if(!qad_seek(c,c->position)){c->active=false;return 0;}}
    return c->adpcm_predictor/256;
}
static bool channel_advance(qmix_channel_t *c){
    uint32_t next=c->position+1;
    if(next==c->sound.length){if(c->sound.loop<0){c->active=false;return false;}next=(uint32_t)c->sound.loop;}
    if(c->sound.format==QSOUND_QAD1){
        if(next!=c->position+1||next/c->sound.block_samples!=c->position/c->sound.block_samples){c->adpcm_valid=false;}
        else{unsigned count;const uint8_t *p=qad_block(&c->sound,c->position/c->sound.block_samples,&count);unsigned within=c->position%c->sound.block_samples;if(!p||within+1>=count){c->adpcm_valid=false;}else{uint8_t packed=p[6+within/2],code=(within&1)?packed>>4:packed&15;c->adpcm_predictor=ima_decode(code,c->adpcm_predictor,&c->adpcm_index);++c->adpcm_position;}}
    }
    c->position=next;return true;
}
void qmix_start(qmix_t *m,unsigned channel,const qsound_t *s,unsigned left,unsigned right){
    if(!m||channel>=QMIX_CHANNELS||!s||!s->data||!s->length||s->loop < -1||(s->loop>=0&&(uint32_t)s->loop>=s->length)||
       (s->format==QSOUND_PCM&&((s->rate!=11025&&s->rate!=22050)||(s->width!=1&&s->width!=2)))||
       (s->format==QSOUND_QAD1&&(s->rate!=11025||s->block_samples!=QSOUND_QAD1_BLOCK_SAMPLES)))return;
    m->channels[channel]=(qmix_channel_t){.sound=*s,.left=left>255?255:left,.right=right>255?255:right,.active=true};
}
void qmix_stop(qmix_t *m,unsigned channel){if(m&&channel<QMIX_CHANNELS)m->channels[channel].active=false;}
void qmix_render(qmix_t *m,uint16_t *pwm,size_t samples){
    for(size_t i=0;i<samples;++i){int value=0;for(unsigned j=0;j<QMIX_CHANNELS;++j){qmix_channel_t *c=&m->channels[j];if(!c->active)continue;int sample=channel_sample(c);if(!c->active)continue;value+=sample*(int)(c->left+c->right)/510;c->phase+=c->sound.rate;if(c->phase>=QMIX_RATE){c->phase-=QMIX_RATE;channel_advance(c);}}
        if(value>127)value=127;if(value<-128)value=-128;pwm[i]=(uint16_t)(value+128);}
}
