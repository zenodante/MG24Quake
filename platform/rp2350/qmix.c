#include "qmix.h"
#include <string.h>
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static unsigned rd16(const uint8_t *p) { return p[0]|(unsigned)p[1]<<8; }
bool qsound_wav(qsound_t *s,const uint8_t *wav,size_t bytes) {
    if(!s || !wav || bytes<12 || memcmp(wav,"RIFF",4) || memcmp(wav+8,"WAVE",4)) return false;
    qsound_t out={.loop=-1}; bool format=false;
    uint32_t loop_length=0;
    for(size_t pos=12;pos<=bytes && bytes-pos>=8;) {
        const uint8_t *h=wav+pos,*d=h+8; uint32_t n=rd32(h+4);
        if(n>bytes-pos-8) break; // Historical shareware WAVs have malformed trailing metadata.
        if(!memcmp(h,"fmt ",4)) {
            if(n<16 || rd16(d)!=1 || rd16(d+2)!=1 ||
               (rd16(d+14)!=8 && rd16(d+14)!=16)) return false;
            out.width=rd16(d+14)/8;
            if(rd16(d+12)!=out.width)return false;
            out.rate=rd32(d+4); if(out.rate!=11025 && out.rate!=22050) return false;
            format=true;
        } else if(!memcmp(h,"data",4)) {out.pcm=d;out.length=n;}
        else if(!memcmp(h,"cue ",4) && n>=28 && rd32(d)>0) out.loop=(int32_t)rd32(d+24);
        else if(!memcmp(h,"LIST",4) && n>=24 && !memcmp(d+20,"mark",4)) loop_length=rd32(d+16);
        pos+=8+n+(n&1u);
    }
    if(!format || !out.pcm || !out.length || out.loop < -1 || out.length%out.width) return false;
    out.length/=out.width;
    if(out.loop>=0) {
        if((uint32_t)out.loop>=out.length) return false;
        if(loop_length) {
            if(loop_length>out.length-(uint32_t)out.loop) return false;
            out.length=(uint32_t)out.loop+loop_length;
        }
    }
    *s=out; return true;
}
void qmix_start(qmix_t *m,unsigned channel,const qsound_t *s,unsigned left,unsigned right) {
    if(channel>=QMIX_CHANNELS || !s || !s->pcm || !s->length ||
       (s->rate!=11025 && s->rate!=22050) || (s->width!=1 && s->width!=2) || s->loop < -1 ||
       (s->loop>=0 && (uint32_t)s->loop>=s->length)) return;
    m->channels[channel]=(qmix_channel_t){.sound=*s,.left=left>255?255:left,
                                          .right=right>255?255:right,.active=true};
}
void qmix_stop(qmix_t *m,unsigned channel) {
    if(channel<QMIX_CHANNELS) m->channels[channel].active=false;
}
void qmix_render(qmix_t *m,uint16_t *pwm,size_t samples) {
    for(size_t i=0;i<samples;++i) {
        int value=0;
        for(unsigned j=0;j<QMIX_CHANNELS;++j) {
            qmix_channel_t *c=&m->channels[j]; if(!c->active)continue;
            int sample=c->sound.width==1 ? (int)c->sound.pcm[c->position]-128 :
                (int)(int16_t)rd16(c->sound.pcm+2*c->position)/256;
            value+=sample*(int)(c->left+c->right)/510;
            c->phase+=c->sound.rate;
            if(c->phase>=QMIX_RATE) {
                c->phase-=QMIX_RATE;
                if(++c->position==c->sound.length) {
                    if(c->sound.loop>=0)c->position=(uint32_t)c->sound.loop;
                    else c->active=false;
                }
            }
        }
        if(value>127)value=127;
        if(value < -128)value=-128;
        pwm[i]=(uint16_t)(value+128);
    }
}
