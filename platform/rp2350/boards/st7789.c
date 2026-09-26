#include "st7789.h"
#include "config.h"

#include <math.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "pico/time.h"

#include "st7789_parallel.pio.h"

// ---------------------------------------------------------------------------
// Module-private state. Pins are compile-time constants (see st7789.h) so
// they cost no RAM; only the handful of values that change at runtime are
// kept here.
// ---------------------------------------------------------------------------
static PIO     st_pio;
static uint8_t st_sm;
static uint8_t st_offset;
static uint8_t st_dma;

enum {
    MADCTL_ROW_ORDER   = 0x80,
    MADCTL_COL_ORDER   = 0x40,
    MADCTL_SWAP_XY     = 0x20, // AKA "MV"
    MADCTL_SCAN_ORDER  = 0x10,
    MADCTL_RGB_BGR     = 0x08,
    MADCTL_HORIZ_ORDER = 0x04,
};

enum {
    REG_SWRESET  = 0x01,
    REG_TEOFF    = 0x34,
    REG_TEON     = 0x35,
    REG_MADCTL   = 0x36,
    REG_COLMOD   = 0x3A,
    REG_RAMCTRL  = 0xB0,
    REG_GCTRL    = 0xB7,
    REG_VCOMS    = 0xBB,
    REG_LCMCTRL  = 0xC0,
    REG_VDVVRHEN = 0xC2,
    REG_VRHS     = 0xC3,
    REG_VDVS     = 0xC4,
    REG_FRCTRL2  = 0xC6,
    REG_PWCTRL1  = 0xD0,
    REG_PORCTRL  = 0xB2,
    REG_GMCTRP1  = 0xE0,
    REG_GMCTRN1  = 0xE1,
    REG_INVOFF   = 0x20,
    REG_SLPOUT   = 0x11,
    REG_DISPON   = 0x29,
    REG_GAMSET   = 0x26,
    REG_DISPOFF  = 0x28,
    REG_RAMWR    = 0x2C,
    REG_INVON    = 0x21,
    REG_CASET    = 0x2A,
    REG_RASET    = 0x2B,
    REG_PWMFRSEL = 0xCC,
};

// Waits only until the *previous* transfer has been handed off to DMA, then
// triggers this one and returns without waiting for it to complete.
void st7789_write_dma(const uint8_t *data, size_t len) {
    while (dma_channel_is_busy(st_dma))
        ;

    dma_channel_set_trans_count(st_dma, len, false);
    dma_channel_set_read_addr(st_dma, data, true);
}

bool st7789_dma_busy(void) {
    return dma_channel_is_busy(st_dma);
}

void st7789_dma_wait(void) {
    dma_channel_wait_for_finish_blocking(st_dma);

    // DMA completion means only that the source buffer has been consumed.
    // Wait until the final byte has left the FIFO, then allow substantially
    // more than the two 32 MHz PIO cycles needed for OUT + WR rising edge.
    // This avoids relying on the sticky TXSTALL flag, which may describe the
    // idle stall that existed before the transfer started.
    while (!pio_sm_is_tx_fifo_empty(st_pio, st_sm))
        ;
    busy_wait_us_32(1);
}

void st7789_start_write(void) {
    gpio_put(ST7789_PIN_DC, 0); // command mode
    gpio_put(ST7789_PIN_CS, 0);

    uint8_t cmd = REG_RAMWR;
    st7789_write_dma(&cmd, 1);
    st7789_dma_wait();

    gpio_put(ST7789_PIN_DC, 1); // data mode, ready for pixel bytes
}

void st7789_end_write(void) {
    st7789_dma_wait();
    gpio_put(ST7789_PIN_CS, 1);
}

void st7789_write_pixels(const uint8_t *data, size_t len) {
    st7789_start_write();
    st7789_write_dma(data, len);
    st7789_end_write();
}

void st7789_command(uint8_t cmd, const uint8_t *data, size_t len) {
    gpio_put(ST7789_PIN_DC, 0); // command mode
    gpio_put(ST7789_PIN_CS, 0);

    st7789_write_dma(&cmd, 1);
    st7789_dma_wait();

    if (data && len) {
        gpio_put(ST7789_PIN_DC, 1); // data mode
        st7789_write_dma(data, len);
        st7789_dma_wait();
    }

    gpio_put(ST7789_PIN_CS, 1);
}



void st7789_update(const uint8_t *frame, size_t len) {
    st7789_write_pixels(frame, len);
}

void st7789_set_backlight(uint8_t brightness) {
    // Gamma correct the 0-255 input onto the 0-65535 PWM range.
    const float gamma = 2.8f;
    uint16_t value = (uint16_t)(powf((float)brightness / 255.0f, gamma) * 65535.0f + 0.5f);
    pwm_set_gpio_level(ST7789_PIN_BL, value);
}

// Sets CASET/RASET/MADCTL for the given panel size/rotation. caset/raset/
// madctl are transient stack values, never kept around between calls.
static void configure_display(uint16_t width, uint16_t height, st7789_rotation_t rotate, bool round) {
    if (rotate == ST7789_ROTATE_90 || rotate == ST7789_ROTATE_270) {
        uint16_t tmp = width;
        width = height;
        height = tmp;
    }

    uint16_t caset[2] = {0, 0};
    uint16_t raset[2] = {0, 0};
    uint8_t madctl = 0;

    // 240x240 square and round LCD breakouts
    if (width == 240 && height == 240) {
        int row_offset = round ? 40 : 80;
        int col_offset = 0;

        switch (rotate) {
            case ST7789_ROTATE_90:
                if (!round) row_offset = 0;
                caset[0] = (uint16_t)row_offset;
                caset[1] = (uint16_t)(width + row_offset - 1);
                raset[0] = (uint16_t)col_offset;
                raset[1] = (uint16_t)(width + col_offset - 1);
                madctl = MADCTL_HORIZ_ORDER | MADCTL_COL_ORDER | MADCTL_SWAP_XY;
                break;
            case ST7789_ROTATE_180:
                caset[0] = (uint16_t)col_offset;
                caset[1] = (uint16_t)(width + col_offset - 1);
                raset[0] = (uint16_t)row_offset;
                raset[1] = (uint16_t)(width + row_offset - 1);
                madctl = MADCTL_HORIZ_ORDER | MADCTL_COL_ORDER | MADCTL_ROW_ORDER;
                break;
            case ST7789_ROTATE_270:
                caset[0] = (uint16_t)row_offset;
                caset[1] = (uint16_t)(width + row_offset - 1);
                raset[0] = (uint16_t)col_offset;
                raset[1] = (uint16_t)(width + col_offset - 1);
                madctl = MADCTL_ROW_ORDER | MADCTL_SWAP_XY;
                break;
            default: // ST7789_ROTATE_0
                if (!round) row_offset = 0;
                caset[0] = (uint16_t)col_offset;
                caset[1] = (uint16_t)(width + col_offset - 1);
                raset[0] = (uint16_t)row_offset;
                raset[1] = (uint16_t)(width + row_offset - 1);
                madctl = MADCTL_HORIZ_ORDER;
                break;
        }
    }

    // Pico Display (1.14" 240x135)
    if (width == 240 && height == 135) {
        caset[0] = 40;
        caset[1] = (uint16_t)(40 + width - 1);
        raset[0] = 52;
        raset[1] = (uint16_t)(52 + height - 1);
        if (rotate == ST7789_ROTATE_0) {
            raset[0] += 1;
            raset[1] += 1;
        }
        madctl = (rotate == ST7789_ROTATE_180) ? MADCTL_ROW_ORDER : MADCTL_COL_ORDER;
        madctl |= MADCTL_SWAP_XY | MADCTL_SCAN_ORDER;
    }

    // Pico Display at 90 degree rotation
    if (width == 135 && height == 240) {
        caset[0] = 52;
        caset[1] = (uint16_t)(52 + width - 1);
        raset[0] = 40;
        raset[1] = (uint16_t)(40 + height - 1);
        madctl = 0;
        if (rotate == ST7789_ROTATE_90) {
            caset[0] += 1;
            caset[1] += 1;
            madctl = MADCTL_COL_ORDER | MADCTL_ROW_ORDER;
        }
    }

    // Pico Display 2.0
    if (width == 320 && height == 240) {
        caset[0] = 0;
        caset[1] = 319;
        raset[0] = 0;
        raset[1] = 239;
        madctl = (rotate == ST7789_ROTATE_180 || rotate == ST7789_ROTATE_90) ? MADCTL_ROW_ORDER : MADCTL_COL_ORDER;
        madctl |= MADCTL_SWAP_XY | MADCTL_SCAN_ORDER;
    }

    // Pico Display 2.0 at 90 degree rotation
    if (width == 240 && height == 320) {
        caset[0] = 0;
        caset[1] = 239;
        raset[0] = 0;
        raset[1] = 319;
        madctl = (rotate == ST7789_ROTATE_180 || rotate == ST7789_ROTATE_90) ? (MADCTL_COL_ORDER | MADCTL_ROW_ORDER) : 0;
    }

    // Byte swap the 16 bit rows/cols values
    caset[0] = __builtin_bswap16(caset[0]);
    caset[1] = __builtin_bswap16(caset[1]);
    raset[0] = __builtin_bswap16(raset[0]);
    raset[1] = __builtin_bswap16(raset[1]);

    st7789_command(REG_CASET, (const uint8_t *)caset, 4);
    st7789_command(REG_RASET, (const uint8_t *)raset, 4);
    st7789_command(REG_MADCTL, &madctl, 1);
}

// Address a x,y/w,h rectangle in raw panel CASET/RASET coordinates for a
// partial update. If the panel/rotation uses a start offset (round variant,
// Pico Display packs), the caller adds that fixed constant to x/y itself.
// caset/raset are transient stack values, nothing is kept in RAM.
void st7789_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    uint16_t caset[2] = {
        __builtin_bswap16(x),
        __builtin_bswap16((uint16_t)(x + w - 1)),
    };
    uint16_t raset[2] = {
        __builtin_bswap16(y),
        __builtin_bswap16((uint16_t)(y + h - 1)),
    };

    st7789_command(REG_CASET, (const uint8_t *)caset, 4);
    st7789_command(REG_RASET, (const uint8_t *)raset, 4);
}

void st7789_init(uint16_t width, uint16_t height, st7789_rotation_t rotation, bool round,
                  st7789_color_format_t color_format) {
    st_pio = pio1;
    pio_set_gpio_base(st_pio, (ST7789_PIN_D0 + 8) >= 32 ? 16 : 0);
    st_sm = (uint8_t)pio_claim_unused_sm(st_pio, true);
    st_offset = (uint8_t)pio_add_program(st_pio, &st7789_parallel_program);

    pio_gpio_init(st_pio, ST7789_PIN_WR_SCK);

    gpio_set_function(ST7789_PIN_RD_SCK, GPIO_FUNC_SIO);
    gpio_set_dir(ST7789_PIN_RD_SCK, GPIO_OUT);

    for (uint i = 0; i < 8; i++) {
        pio_gpio_init(st_pio, ST7789_PIN_D0 + i);
    }

    pio_sm_set_consecutive_pindirs(st_pio, st_sm, ST7789_PIN_D0, 8, true);
    pio_sm_set_consecutive_pindirs(st_pio, st_sm, ST7789_PIN_WR_SCK, 1, true);

    pio_sm_config c = st7789_parallel_program_get_default_config(st_offset);
    sm_config_set_out_pins(&c, ST7789_PIN_D0, 8);
    sm_config_set_sideset_pins(&c, ST7789_PIN_WR_SCK);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_out_shift(&c, false, true, 8);

    // Keep the LCD bus rate constant when clk_sys switches between the 200 and
    // 250 MHz profiles. This PIO program takes two SM cycles per WR strobe, so
    // the default 32 MHz SM clock produces a 16 MHz parallel write strobe.
    const uint32_t sys_clk_hz = clock_get_hz(clk_sys);
    const float clk_div = (float)sys_clk_hz / (float)P8_RP2350_LCD_PIO_HZ;
    sm_config_set_clkdiv(&c, clk_div);

    pio_sm_init(st_pio, st_sm, st_offset, &c);
    pio_sm_set_enabled(st_pio, st_sm, true);

    st_dma = (uint8_t)dma_claim_unused_channel(true);
    dma_channel_config dma_cfg = dma_channel_get_default_config(st_dma);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_8);
    channel_config_set_bswap(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg, pio_get_dreq(st_pio, st_sm, true));
    dma_channel_configure(st_dma, &dma_cfg, &st_pio->txf[st_sm], NULL, 0, false);

    gpio_put(ST7789_PIN_RD_SCK, 1);

    gpio_set_function(ST7789_PIN_DC, GPIO_FUNC_SIO);
    gpio_set_dir(ST7789_PIN_DC, GPIO_OUT);

    gpio_set_function(ST7789_PIN_CS, GPIO_FUNC_SIO);
    gpio_set_dir(ST7789_PIN_CS, GPIO_OUT);

    // Backlight PWM, off until the panel has been initialised.
    pwm_config pwm_cfg = pwm_get_default_config();
    pwm_set_wrap(pwm_gpio_to_slice_num(ST7789_PIN_BL), 65535);
    pwm_init(pwm_gpio_to_slice_num(ST7789_PIN_BL), &pwm_cfg, true);
    gpio_set_function(ST7789_PIN_BL, GPIO_FUNC_PWM);
    st7789_set_backlight(0);

    st7789_command(REG_SWRESET, NULL, 0);
    sleep_ms(150);

    // COLMOD control-interface bits: 101 = 16 bit/pixel (RGB565), 110 = 18 bit/pixel (RGB666)
    uint8_t colmod = (color_format == ST7789_COLOR_RGB666) ? 0x06 : 0x05;

    st7789_command(REG_TEON, NULL, 0);
    st7789_command(REG_COLMOD, &colmod, 1);

    st7789_command(REG_PORCTRL, (const uint8_t[]){0x0c, 0x0c, 0x00, 0x33, 0x33}, 5);
    st7789_command(REG_LCMCTRL, (const uint8_t[]){0x2c}, 1);
    st7789_command(REG_VDVVRHEN, (const uint8_t[]){0x01}, 1);
    st7789_command(REG_VRHS, (const uint8_t[]){0x12}, 1);
    st7789_command(REG_VDVS, (const uint8_t[]){0x20}, 1);
    st7789_command(REG_PWCTRL1, (const uint8_t[]){0xa4, 0xa1}, 2);
    st7789_command(REG_FRCTRL2, (const uint8_t[]){0x0f}, 1);

    // Fixes a light grey banding issue with low brightness green (pimoroni/pimoroni-pico#1040)
    st7789_command(REG_RAMCTRL, (const uint8_t[]){0x00, 0xc0}, 2);

    if (width == 240 && height == 240) {
        st7789_command(REG_GCTRL, (const uint8_t[]){0x14}, 1);
        st7789_command(REG_VCOMS, (const uint8_t[]){0x37}, 1);
        st7789_command(REG_GMCTRP1, (const uint8_t[]){0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23}, 14);
        st7789_command(REG_GMCTRN1, (const uint8_t[]){0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23}, 14);
    }

    if (width == 320 && height == 240) {
        st7789_command(REG_GCTRL, (const uint8_t[]){0x35}, 1);
        st7789_command(REG_VCOMS, (const uint8_t[]){0x1f}, 1);
        st7789_command(REG_GMCTRP1, (const uint8_t[]){0xD0, 0x08, 0x11, 0x08, 0x0C, 0x15, 0x39, 0x33, 0x50, 0x36, 0x13, 0x14, 0x29, 0x2D}, 14);
        st7789_command(REG_GMCTRN1, (const uint8_t[]){0xD0, 0x08, 0x10, 0x08, 0x06, 0x06, 0x39, 0x44, 0x51, 0x0B, 0x16, 0x14, 0x2F, 0x31}, 14);
    }

    if (width == 240 && height == 135) { // Pico Display Pack (1.14" 240x135)
        st7789_command(REG_VRHS, (const uint8_t[]){0x00}, 1);
        st7789_command(REG_GCTRL, (const uint8_t[]){0x75}, 1);
        st7789_command(REG_VCOMS, (const uint8_t[]){0x3D}, 1);
        st7789_command(0xd6, (const uint8_t[]){0xa1}, 1);
        st7789_command(REG_GMCTRP1, (const uint8_t[]){0x70, 0x04, 0x08, 0x09, 0x09, 0x05, 0x2A, 0x33, 0x41, 0x07, 0x13, 0x13, 0x29, 0x2f}, 14);
        st7789_command(REG_GMCTRN1, (const uint8_t[]){0x70, 0x03, 0x09, 0x0A, 0x09, 0x06, 0x2B, 0x34, 0x41, 0x07, 0x12, 0x14, 0x28, 0x2E}, 14);
    }

    st7789_command(REG_INVON, NULL, 0);  // set inversion mode
    st7789_command(REG_SLPOUT, NULL, 0); // leave sleep mode
    st7789_command(REG_DISPON, NULL, 0); // turn display on

    sleep_ms(100);

    configure_display(width, height, rotation, round);

    sleep_ms(50); // wait for the update to apply
    st7789_set_backlight(255);
}

void st7789_deinit(void) {
    if (dma_channel_is_claimed(st_dma)) {
        dma_channel_abort(st_dma);
        dma_channel_unclaim(st_dma);
    }

    if (pio_sm_is_claimed(st_pio, st_sm)) {
        pio_sm_set_enabled(st_pio, st_sm, false);
        pio_sm_drain_tx_fifo(st_pio, st_sm);
        pio_sm_unclaim(st_pio, st_sm);
    }

    pio_remove_program(st_pio, &st7789_parallel_program, st_offset);
}
