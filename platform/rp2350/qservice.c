#include "qservice.h"
#include "boards/st7789.h"
#include "boards/qwstpad.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <string.h>
#include <assert.h>

enum { FREE,DRAWING,READY,DISPLAYING, QUEUE_SIZE=32, LCD_ROWS=1 };
typedef struct {
    uint32_t state,sequence;
    uint8_t pixels[Q_FRAME_BYTES],palette[768];
} frame_t;
typedef struct { unsigned op,channel,left,right; qsound_t sound; uint32_t fence; } sound_cmd_t;
static frame_t frames[2];
static uint32_t submitted,started,input_write,input_read,input_held,sound_write,sound_read,sound_fence;
static qinput_event_t input_queue[QUEUE_SIZE];
static sound_cmd_t sound_queue[QUEUE_SIZE];
static qservice_stats_t stats;
static qmix_t mixer;
static uint16_t lcd_row0[Q_WIDTH],lcd_row1[Q_WIDTH],lut[256];
static uint16_t audio[2][QMIX_BLOCK];
static int audio_dma[2];
static unsigned audio_slice;
static volatile bool audio_queued[2];
static unsigned next_audio;
static bool audio_ever_started;
static qwstpad_t pad;
static bool pad_ready;
static uint32_t next_input,next_pad_retry;
static int active_frame=-1;
static unsigned lcd_y,lcd_slot;
static uint32_t load(const uint32_t *p) { return __atomic_load_n(p,__ATOMIC_ACQUIRE); }
static void publish(uint32_t *p,uint32_t v) { __atomic_store_n(p,v,__ATOMIC_RELEASE); }
static void counter(uint32_t *p) { __atomic_fetch_add(p,1,__ATOMIC_RELAXED); }

static void audio_irq(void) {
    for(unsigned i=0;i<2;++i) {
        uint32_t mask=1u<<(unsigned)audio_dma[i];
        if(dma_hw->ints0&mask) {dma_hw->ints0=mask;audio_queued[i]=false;}
    }
}
static void audio_init(void) {
    gpio_init(13);gpio_set_dir(13,GPIO_OUT);gpio_put(13,0);
    gpio_set_function(12,GPIO_FUNC_PWM);audio_slice=pwm_gpio_to_slice_num(12);
    pwm_config cfg=pwm_get_default_config();pwm_config_set_wrap(&cfg,255);
    uint32_t den=QMIX_RATE*256u;
    uint32_t div16=(uint32_t)(((uint64_t)clock_get_hz(clk_sys)*16+den/2)/den);
    pwm_config_set_clkdiv_int_frac4(&cfg,div16>>4,div16&15);
    pwm_init(audio_slice,&cfg,false);pwm_set_gpio_level(12,128);
    for(unsigned i=0;i<2;++i)audio_dma[i]=dma_claim_unused_channel(true);
    for(unsigned i=0;i<2;++i) {
        dma_channel_config dc=dma_channel_get_default_config(audio_dma[i]);
        channel_config_set_transfer_data_size(&dc,DMA_SIZE_16);
        channel_config_set_read_increment(&dc,true);channel_config_set_write_increment(&dc,false);
        channel_config_set_dreq(&dc,pwm_get_dreq(audio_slice));
        channel_config_set_chain_to(&dc,audio_dma[i^1]);
        dma_channel_configure(audio_dma[i],&dc,&pwm_hw->slice[audio_slice].cc,audio[i],0,false);
        dma_hw->ints0=1u<<audio_dma[i];dma_channel_set_irq0_enabled(audio_dma[i],true);
    }
    irq_set_exclusive_handler(DMA_IRQ_0,audio_irq);irq_set_enabled(DMA_IRQ_0,true);
    pwm_set_enabled(audio_slice,true);
}
static void sound_commands(void) {
    uint32_t r=load(&sound_read),w=load(&sound_write);
    while(r!=w) {
        sound_cmd_t c=sound_queue[r%QUEUE_SIZE];
        if(c.op==1)qmix_start(&mixer,c.channel,&c.sound,c.left,c.right);
        else if(c.op==2)qmix_stop(&mixer,c.channel);
        else if(c.op==3) {
            memset(&mixer,0,sizeof mixer);
            // No queued output contains PCM pointers, only already mixed samples.
            publish(&sound_fence,c.fence);
        }
        publish(&sound_read,++r);
    }
}
static void audio_service(void) {
    sound_commands();
    for(unsigned n=0;n<2;++n) {
        unsigned i=next_audio;
        if(audio_queued[i])break;
        qmix_render(&mixer,audio[i],QMIX_BLOCK);
        uint32_t irq=save_and_disable_interrupts();
        audio_queued[i]=true;
        dma_channel_set_read_addr(audio_dma[i],audio[i],false);
        dma_channel_set_trans_count(audio_dma[i],QMIX_BLOCK,false);
        if(!dma_channel_is_busy(audio_dma[0]) && !dma_channel_is_busy(audio_dma[1])) {
            if(audio_ever_started)counter(&stats.audio_underruns);
            dma_start_channel_mask(1u<<audio_dma[i]);audio_ever_started=true;
        }
        restore_interrupts(irq);next_audio^=1;counter(&stats.audio_blocks);
    }
}
static void input_service(void) {
    uint32_t now=time_us_32();if((int32_t)(now-next_input)<0)return;
    next_input=now+5000;
    if(!pad_ready && (int32_t)(now-next_pad_retry)>=0) {
        pad_ready=qwstpad_init(&pad,i2c0,0x21,false);next_pad_retry=now+1000000;
    }
    uint16_t held=0;
    if(pad_ready && !qwstpad_read_buttons(&pad,&held)) {
        held=0;pad_ready=false;next_pad_retry=now+1000000;counter(&stats.input_errors);
    }
    uint16_t old=(uint16_t)load(&input_held);
    if(held!=old) {
        publish(&input_held,held);
        uint32_t w=load(&input_write),r=load(&input_read);
        if(w-r<QUEUE_SIZE) {input_queue[w%QUEUE_SIZE]=(qinput_event_t){now,held};publish(&input_write,w+1);}
        else counter(&stats.input_overflows); // Consumer can resynchronize from held snapshot.
    }
}
static void video_service(void) {
    if(active_frame<0) {
        int chosen=-1;
        for(unsigned i=0;i<2;++i)if(load(&frames[i].state)==READY &&
            (chosen<0 || (int32_t)(frames[i].sequence-frames[chosen].sequence)<0))chosen=(int)i;
        if(chosen<0)return;
        active_frame=chosen;publish(&frames[chosen].state,DISPLAYING);
        const uint8_t *p=frames[chosen].palette;
        for(unsigned i=0;i<256;++i) {
            uint16_t color=((p[0]>>3)<<11)|((p[1]>>2)<<5)|(p[2]>>3);p+=3;
            lut[i]=(color>>8)|(color<<8);
        }
        lcd_y=0;lcd_slot=0;st7789_set_window(0,20,Q_WIDTH,Q_HEIGHT);st7789_start_write();
    }
    if(st7789_dma_busy())return;
    if(lcd_y==Q_HEIGHT) {
        st7789_end_write();publish(&frames[active_frame].state,FREE);active_frame=-1;
        counter(&stats.frames);return;
    }
    const uint8_t *src=frames[active_frame].pixels+lcd_y*Q_WIDTH;
    uint16_t *dst=lcd_slot?lcd_row1:lcd_row0;
    for(unsigned i=0;i<Q_WIDTH;++i)dst[i]=lut[src[i]];
    st7789_write_dma((const uint8_t *)dst,sizeof lcd_row0);
    lcd_slot^=1;++lcd_y;
}
static void worker(void) {
    i2c_init(i2c0,400000);
    gpio_set_function(20,GPIO_FUNC_I2C);gpio_set_function(21,GPIO_FUNC_I2C);
    gpio_pull_up(20);gpio_pull_up(21);
    st7789_init(320,240,ST7789_ROTATE_0,false,ST7789_COLOR_RGB565);
    st7789_set_backlight(0);
    memset(lcd_row0,0,sizeof lcd_row0);st7789_set_window(0,0,320,240);st7789_start_write();
    for(unsigned y=0;y<240;++y)st7789_write_dma((const uint8_t *)lcd_row0,sizeof lcd_row0);
    st7789_end_write();st7789_set_backlight(255);
    audio_init();audio_service();busy_wait_us_32(1000);gpio_put(13,1);
    publish(&started,1);
    for(;;) {audio_service();input_service();video_service();tight_loop_contents();}
}
bool qservice_start(void) {
    if(load(&started))return true;
    multicore_launch_core1(worker);
    uint32_t deadline=time_us_32()+3000000;
    while(!load(&started)) {if((int32_t)(time_us_32()-deadline)>=0)return false;tight_loop_contents();}
    return true;
}
uint8_t *qservice_frame_acquire(unsigned *slot) {
    for(unsigned i=0;i<2;++i)if(load(&frames[i].state)==FREE) {
        publish(&frames[i].state,DRAWING);*slot=i;return frames[i].pixels;
    }
    return NULL;
}
void qservice_frame_submit(unsigned slot,const uint8_t palette[768]) {
    assert(slot<2 && load(&frames[slot].state)==DRAWING);
    memcpy(frames[slot].palette,palette,768);frames[slot].sequence=submitted++;
    publish(&frames[slot].state,READY);
}
bool qservice_input_pop(qinput_event_t *e) {
    uint32_t r=load(&input_read);if(r==load(&input_write))return false;
    *e=input_queue[r%QUEUE_SIZE];publish(&input_read,r+1);return true;
}
uint16_t qservice_buttons(void) {return (uint16_t)load(&input_held);}
static bool sound_submit(sound_cmd_t c) {
    uint32_t w=load(&sound_write);if(w-load(&sound_read)>=QUEUE_SIZE)return false;
    sound_queue[w%QUEUE_SIZE]=c;publish(&sound_write,w+1);return true;
}
bool qservice_sound_start(unsigned channel,const qsound_t *s,unsigned left,unsigned right) {
    if(channel>=QMIX_CHANNELS || !s)return false;
    return sound_submit((sound_cmd_t){.op=1,.channel=channel,.left=left,.right=right,.sound=*s});
}
bool qservice_sound_stop(unsigned channel) {
    if(channel>=QMIX_CHANNELS)return false;
    return sound_submit((sound_cmd_t){.op=2,.channel=channel});
}
bool qservice_sound_stop_all(uint32_t fence) {return sound_submit((sound_cmd_t){.op=3,.fence=fence});}
bool qservice_sound_fence_done(uint32_t fence) {return load(&sound_fence)==fence;}
void qservice_stats(qservice_stats_t *s) {
    s->frames=load(&stats.frames);s->audio_blocks=load(&stats.audio_blocks);
    s->audio_underruns=load(&stats.audio_underruns);s->input_errors=load(&stats.input_errors);
    s->input_overflows=load(&stats.input_overflows);
}
