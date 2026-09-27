#include "qmix.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void wr16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void wr32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
int main(void){
    qmix_t mixer={0};uint16_t out[16];uint8_t pcm[]={0,128,255};
    qsound_t sound={.data=pcm,.bytes=sizeof pcm,.length=3,.rate=11025,.loop=-1,.width=1,.format=QSOUND_PCM};
    qmix_start(&mixer,0,&sound,255,255);qmix_render(&mixer,out,8);const uint16_t expected[]={0,0,128,128,255,255,128,128};assert(!memcmp(out,expected,sizeof expected));
    sound.loop=1;qmix_start(&mixer,0,&sound,255,255);qmix_render(&mixer,out,10);const uint16_t loop[]={0,0,128,128,255,255,128,128,255,255};assert(!memcmp(out,loop,sizeof loop));
    qmix_stop(&mixer,0);qmix_render(&mixer,out,16);for(unsigned i=0;i<16;++i)assert(out[i]==128);
    uint8_t loud[]={255,255};sound=(qsound_t){.data=loud,.bytes=2,.length=2,.rate=22050,.loop=0,.width=1,.format=QSOUND_PCM};qmix_start(&mixer,0,&sound,255,255);qmix_start(&mixer,1,&sound,255,255);qmix_render(&mixer,out,16);for(unsigned i=0;i<16;++i)assert(out[i]==255);
    qsound_t invalid={.data=loud,.bytes=2,.length=2,.rate=22050,.loop=2,.width=1,.format=QSOUND_PCM};memset(&mixer,0,sizeof mixer);qmix_start(&mixer,0,&invalid,255,255);assert(!mixer.channels[0].active);
    uint8_t pcm16[]={0,128,0,0,0,127};sound=(qsound_t){.data=pcm16,.bytes=6,.length=3,.rate=22050,.loop=-1,.width=2,.format=QSOUND_PCM};qmix_start(&mixer,0,&sound,255,255);qmix_render(&mixer,out,4);assert(out[0]==0&&out[1]==128&&out[2]==255&&out[3]==128);

    /* QAD1: four samples, predictor 0, a useful starting step, then codes 7,7,15. */
    uint8_t qad[24]={0};memcpy(qad,"QAD1",4);wr32(qad+4,4);wr32(qad+8,0xffffffffu);wr16(qad+12,256);wr16(qad+16,0);qad[18]=40;qad[19]=0;wr16(qad+20,4);qad[22]=0x77;qad[23]=0x0f;
    assert(qsound_qad1(&sound,qad,sizeof qad));assert(sound.format==QSOUND_QAD1&&sound.length==4&&sound.loop==-1);
    memset(&mixer,0,sizeof mixer);qmix_start(&mixer,0,&sound,255,255);qmix_render(&mixer,out,10);assert(out[0]==128&&out[1]==128);assert(out[2]>128&&out[3]==out[2]);assert(out[4]>out[2]&&out[5]==out[4]);assert(out[8]==128&&out[9]==128);

    /* Loop at sample 1 exercises a non-block-boundary seek and decoder-state rebuild. */
    wr32(qad+8,1);assert(qsound_open(&sound,qad,sizeof qad));memset(&mixer,0,sizeof mixer);qmix_start(&mixer,0,&sound,255,255);qmix_render(&mixer,out,12);assert(out[0]==128&&out[1]==128);assert(out[8]==out[2]&&out[9]==out[3]);
    qad[18]=89;assert(!qsound_qad1(&sound,qad,sizeof qad));qad[18]=40;qad[12]=128;assert(!qsound_qad1(&sound,qad,sizeof qad));
    puts("PASS: PCM mixer plus QAD1 parse, incremental decode, hold timing and loop seek");return 0;
}
