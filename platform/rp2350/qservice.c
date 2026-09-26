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
#include <assert.h>
#include <string.h>

enum {
    FREE,DRAWING,READY,DISPLAYING,
    AUDIO_RING_SAMPLES=1024,
    INPUT_POLL_US=10000
};

typedef struct {
    uint32_t state;
    uint8_t pixels[Q_FRAME_BYTES],palette[768];
} frame_t;

static frame_t frame;
static uint32_t started,sound_fence;
static qservice_stats_t stats;
static qmix_t mixer;

/* Same row-at-a-time LCD ping-pong scheme as p8. */
static uint16_t lcd_row0[Q_WIDTH],lcd_row1[Q_WIDTH],lut[256];
static int lcd_dma_slot=-1;

/* Same audio organization as p8: software ring feeding two chained DMA slots. */
static uint16_t audio_ring[AUDIO_RING_SAMPLES];
static uint16_t audio_samples[2][QMIX_BLOCK];
static volatile uint8_t audio_queued[2];
static int audio_dma[2]={-1,-1};
static unsigned audio_slice,audio_channel,audio_slot;
static size_t audio_ring_read,audio_ring_write,audio_ring_count;
static bool audio_ever_started;
static spin_lock_t *audio_lock;
static uint audio_lock_number;

/* Same input model as p8: current state plus latched rising edges. */
static qwstpad_t input_pad;
static volatile uint16_t input_buttons;
static uint32_t sampled_buttons; /* low 16 held, high 16 latched rising edges */
static uint32_t next_input;

static uint32_t load(const uint32_t *p) { return __atomic_load_n(p,__ATOMIC_ACQUIRE); }
static void publish(uint32_t *p,uint32_t v) { __atomic_store_n(p,v,__ATOMIC_RELEASE); }
static void counter(uint32_t *p) { __atomic_fetch_add(p,1,__ATOMIC_RELAXED); }

static void audio_lock_mixer(void) { spin_lock_unsafe_blocking(audio_lock); }
static void audio_unlock_mixer(void) { spin_unlock_unsafe(audio_lock); }

static void audio_dma_irq(void) {
    for(unsigned slot=0;slot<2;++slot) {
        uint32_t mask=1u<<(unsigned)audio_dma[slot];
        if(dma_hw->ints0&mask) {
            dma_hw->ints0=mask;
            audio_queued[slot]=0;
        }
    }
}

static void audio_hw_init(void) {
    gpio_init(13);gpio_set_dir(13,GPIO_OUT);gpio_put(13,0);
    gpio_set_function(12,GPIO_FUNC_PWM);
    audio_slice=pwm_gpio_to_slice_num(12);
    audio_channel=pwm_gpio_to_channel(12);

    pwm_config config=pwm_get_default_config();
    pwm_config_set_wrap(&config,255);
    uint32_t denominator=QMIX_RATE*256u;
    uint32_t divider16=(uint32_t)(((uint64_t)clock_get_hz(clk_sys)*16u+
                                  denominator/2u)/denominator);
    pwm_config_set_clkdiv_int_frac4(&config,divider16>>4,(uint8_t)(divider16&15u));
    pwm_init(audio_slice,&config,false);
    pwm_set_chan_level(audio_slice,audio_channel,128);

    volatile uint16_t *compare=(volatile uint16_t *)&pwm_hw->slice[audio_slice].cc;
    if(audio_channel==PWM_CHAN_B)++compare;

    audio_dma[0]=dma_claim_unused_channel(true);
    audio_dma[1]=dma_claim_unused_channel(true);
    for(unsigned slot=0;slot<2;++slot) {
        audio_queued[slot]=0;
        dma_channel_config cfg=dma_channel_get_default_config((uint)audio_dma[slot]);
        channel_config_set_transfer_data_size(&cfg,DMA_SIZE_16);
        channel_config_set_read_increment(&cfg,true);
        channel_config_set_write_increment(&cfg,false);
        channel_config_set_dreq(&cfg,pwm_get_dreq(audio_slice));
        channel_config_set_chain_to(&cfg,(uint)audio_dma[slot^1u]);
        dma_channel_configure((uint)audio_dma[slot],&cfg,(void *)compare,
                              audio_samples[slot],0,false);
        dma_hw->ints0=1u<<(unsigned)audio_dma[slot];
        dma_channel_set_irq0_enabled((uint)audio_dma[slot],true);
    }
    irq_set_exclusive_handler(DMA_IRQ_0,audio_dma_irq);
    irq_set_enabled(DMA_IRQ_0,true);
    pwm_set_enabled(audio_slice,true);
}

static void audio_ring_reset(void) {
    audio_ring_read=0;
    audio_ring_write=0;
    audio_ring_count=0;
}

static void audio_ring_fill(size_t target) {
    if(target>AUDIO_RING_SAMPLES)target=AUDIO_RING_SAMPLES;
    if(audio_ring_count>=target)return;
    audio_lock_mixer();
    while(audio_ring_count<target) {
        size_t wanted=target-audio_ring_count;
        size_t contiguous=AUDIO_RING_SAMPLES-audio_ring_write;
        size_t count=wanted<contiguous?wanted:contiguous;
        qmix_render(&mixer,audio_ring+audio_ring_write,count);
        audio_ring_write+=count;
        if(audio_ring_write==AUDIO_RING_SAMPLES)audio_ring_write=0;
        audio_ring_count+=count;
    }
    audio_unlock_mixer();
}

static void audio_ring_consume(size_t count) {
    audio_ring_read+=count;
    if(audio_ring_read==AUDIO_RING_SAMPLES)audio_ring_read=0;
    audio_ring_count-=count;
}

static bool audio_hw_submit(unsigned slot,const uint16_t *samples,size_t count) {
    if(slot>1 || !samples || !count || count>QMIX_BLOCK ||
       audio_dma[slot]<0 || audio_queued[slot])return false;

    for(size_t i=0;i<count;++i)audio_samples[slot][i]=samples[i];

    uint32_t irq_state=save_and_disable_interrupts();
    audio_queued[slot]=1;
    dma_channel_set_read_addr((uint)audio_dma[slot],audio_samples[slot],false);
    dma_channel_set_trans_count((uint)audio_dma[slot],count,false);
    if(!dma_channel_is_busy((uint)audio_dma[0]) &&
       !dma_channel_is_busy((uint)audio_dma[1])) {
        if(audio_ever_started)counter(&stats.audio_underruns);
        dma_start_channel_mask(1u<<(unsigned)audio_dma[slot]);
        audio_ever_started=true;
    }
    restore_interrupts(irq_state);
    return true;
}

static void audio_service(void) {
    for(unsigned n=0;n<2;++n) {
        unsigned slot=audio_slot;
        if(audio_queued[slot])break;
        if(audio_ring_count<QMIX_BLOCK)audio_ring_fill(QMIX_BLOCK);
        if(audio_ring_count<QMIX_BLOCK)break;
        if(!audio_hw_submit(slot,audio_ring+audio_ring_read,QMIX_BLOCK))break;
        audio_ring_consume(QMIX_BLOCK);
        audio_slot^=1u;
        counter(&stats.audio_blocks);
    }
}

static void input_service(void) {
    uint32_t now=time_us_32();
    if((int32_t)(now-next_input)<0)return;

    uint16_t buttons;
    if(!qwstpad_read_buttons(&input_pad,&buttons)) {
        counter(&stats.input_errors);
        next_input=now+INPUT_POLL_US;
        return; /* p8 behavior: keep the last valid state on a failed read. */
    }
    buttons&=0x03ffu;
    __atomic_store_n(&input_buttons,buttons,__ATOMIC_RELEASE);

    uint32_t value=buttons;
    uint32_t old=__atomic_load_n(&sampled_buttons,__ATOMIC_RELAXED),next;
    do {
        uint32_t old_held=old&0xffffu;
        next=value|(old&0xffff0000u)|((value&~old_held)<<16);
    } while(!__atomic_compare_exchange_n(&sampled_buttons,&old,next,false,
                                         __ATOMIC_RELEASE,__ATOMIC_RELAXED));
    next_input=now+INPUT_POLL_US;
}

static void lcd_wait(unsigned slot) {
    if(lcd_dma_slot==(int)slot) {
        st7789_dma_wait();
        lcd_dma_slot=-1;
    }
}

static void lcd_write(unsigned slot,const uint16_t *pixels) {
    lcd_dma_slot=(int)slot;
    st7789_write_dma((const uint8_t *)pixels,Q_WIDTH*sizeof(uint16_t));
}

static void video_present(void) {
    publish(&frame.state,DISPLAYING);

    const uint8_t *p=frame.palette;
    for(unsigned i=0;i<256;++i) {
        uint16_t color=((p[0]>>3)<<11)|((p[1]>>2)<<5)|(p[2]>>3);
        p+=3;
        lut[i]=(color>>8)|(color<<8);
    }

    st7789_set_window(0,20,Q_WIDTH,Q_HEIGHT);
    st7789_start_write();
    unsigned slot=0;
    for(unsigned y=0;y<Q_HEIGHT;++y) {
        lcd_wait(slot);
        uint16_t *dst=slot?lcd_row1:lcd_row0;
        const uint8_t *src=frame.pixels+y*Q_WIDTH;
        for(unsigned x=0;x<Q_WIDTH;++x)dst[x]=lut[src[x]];
        lcd_write(slot,dst);

        /* Exactly as p8: keep audio/input serviced during a long frame transfer. */
        audio_service();
        input_service();
        slot^=1u;
    }
    lcd_wait(0);
    lcd_wait(1);
    st7789_end_write();
    lcd_dma_slot=-1;

    publish(&frame.state,FREE);
    counter(&stats.frames);
}

static void worker(void) {
    i2c_init(i2c0,400000);
    gpio_set_function(20,GPIO_FUNC_I2C);
    gpio_set_function(21,GPIO_FUNC_I2C);
    gpio_pull_up(20);gpio_pull_up(21);
    if(!qwstpad_init(&input_pad,i2c0,0x21,false)) {
        publish(&started,2);
        return;
    }

    audio_lock_number=spin_lock_claim_unused(true);
    audio_lock=spin_lock_instance(audio_lock_number);

    st7789_init(320,240,ST7789_ROTATE_0,false,ST7789_COLOR_RGB565);
    st7789_set_backlight(0);
    memset(lcd_row0,0,sizeof lcd_row0);
    st7789_set_window(0,0,320,240);
    st7789_start_write();
    for(unsigned y=0;y<240;++y)st7789_write_dma((const uint8_t *)lcd_row0,sizeof lcd_row0);
    st7789_end_write();
    st7789_set_backlight(255);

    audio_ring_reset();
    audio_slot=0;
    audio_hw_init();
    audio_service();
    busy_wait_us_32(1000);
    gpio_put(13,1);

    next_input=time_us_32();
    input_service();
    publish(&started,1);

    for(;;) {
        if(load(&frame.state)==READY)video_present();
        input_service();
        audio_service();
        tight_loop_contents();
    }
}

bool qservice_start(void) {
    uint32_t state=load(&started);
    if(state)return state==1;
    multicore_launch_core1(worker);
    uint32_t deadline=time_us_32()+3000000;
    while(!(state=load(&started))) {
        if((int32_t)(time_us_32()-deadline)>=0)return false;
        tight_loop_contents();
    }
    return state==1;
}

uint8_t *qservice_frame_acquire(unsigned *slot) {
    if(load(&frame.state)!=FREE)return NULL;
    publish(&frame.state,DRAWING);
    *slot=0;
    return frame.pixels;
}

void qservice_frame_submit(unsigned slot,const uint8_t palette[768]) {
    assert(slot==0 && load(&frame.state)==DRAWING);
    memcpy(frame.palette,palette,768);
    publish(&frame.state,READY);
}

uint16_t qservice_buttons(void) {
    return __atomic_load_n(&input_buttons,__ATOMIC_ACQUIRE);
}

uint16_t qservice_buttons_pressed(void) {
    uint32_t value=__atomic_fetch_and(&sampled_buttons,0xffffu,__ATOMIC_ACQ_REL);
    return (uint16_t)(value>>16);
}

bool qservice_sound_start(unsigned channel,const qsound_t *sound,unsigned left,unsigned right) {
    if(channel>=QMIX_CHANNELS || !sound)return false;
    audio_lock_mixer();
    qmix_start(&mixer,channel,sound,left,right);
    audio_unlock_mixer();
    return true;
}

bool qservice_sound_stop(unsigned channel) {
    if(channel>=QMIX_CHANNELS)return false;
    audio_lock_mixer();
    qmix_stop(&mixer,channel);
    audio_unlock_mixer();
    return true;
}

bool qservice_sound_stop_all(uint32_t fence) {
    audio_lock_mixer();
    memset(&mixer,0,sizeof mixer);
    audio_unlock_mixer();
    publish(&sound_fence,fence);
    return true;
}

bool qservice_sound_fence_done(uint32_t fence) {
    return load(&sound_fence)==fence;
}

void qservice_stats(qservice_stats_t *s) {
    s->frames=load(&stats.frames);
    s->audio_blocks=load(&stats.audio_blocks);
    s->audio_underruns=load(&stats.audio_underruns);
    s->input_errors=load(&stats.input_errors);
    s->input_overflows=0;
}
