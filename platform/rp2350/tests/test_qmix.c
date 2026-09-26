#include "qmix.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    qmix_t mixer={0};uint16_t out[16];
    uint8_t pcm[]={0,128,255};
    qsound_t sound={.pcm=pcm,.length=3,.rate=11025,.loop=-1,.width=1};
    qmix_start(&mixer,0,&sound,255,255);qmix_render(&mixer,out,8);
    const uint16_t expected[]={0,0,128,128,255,255,128,128};
    assert(!memcmp(out,expected,sizeof expected));
    sound.loop=1;qmix_start(&mixer,0,&sound,255,255);qmix_render(&mixer,out,10);
    const uint16_t loop[]={0,0,128,128,255,255,128,128,255,255};
    assert(!memcmp(out,loop,sizeof loop));
    qmix_stop(&mixer,0);qmix_render(&mixer,out,16);
    for(unsigned i=0;i<16;++i)assert(out[i]==128);
    // Same-rate source advances every output sample; overlapping channels clip.
    uint8_t loud[]={255,255};sound=(qsound_t){.pcm=loud,.length=2,.rate=22050,.loop=0,.width=1};
    qmix_start(&mixer,0,&sound,255,255);qmix_start(&mixer,1,&sound,255,255);
    qmix_render(&mixer,out,16);for(unsigned i=0;i<16;++i)assert(out[i]==255);
    qsound_t invalid={.pcm=loud,.length=2,.rate=22050,.loop=2,.width=1};memset(&mixer,0,sizeof mixer);
    qmix_start(&mixer,0,&invalid,255,255);assert(!mixer.channels[0].active);
    uint8_t pcm16[]={0,128,0,0,0,127};
    sound=(qsound_t){.pcm=pcm16,.length=3,.rate=22050,.loop=-1,.width=2};
    qmix_start(&mixer,0,&sound,255,255);qmix_render(&mixer,out,4);
    assert(out[0]==0 && out[1]==128 && out[2]==255 && out[3]==128);
    puts("PASS: mixer sample timing, looping, stop/silence, saturation, invalid-loop rejection");
    return 0;
}
