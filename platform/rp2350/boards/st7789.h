#ifndef ST7789_H
#define ST7789_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Pin definitions - RP2350, 8-bit parallel interface (PIO driven)
// ---------------------------------------------------------------------------
#define ST7789_PIN_CS       27
#define ST7789_PIN_DC       28
#define ST7789_PIN_WR_SCK   30
#define ST7789_PIN_RD_SCK   31
#define ST7789_PIN_D0       32   // data bus uses D0..D0+7 (32..39)
#define ST7789_PIN_BL       26

typedef enum {
    ST7789_ROTATE_0   = 0,
    ST7789_ROTATE_90  = 1,
    ST7789_ROTATE_180 = 2,
    ST7789_ROTATE_270 = 3,
} st7789_rotation_t;

// COLMOD pixel formats. RGB666 carries each colour channel in the top 6 bits
// of its own byte (bottom 2 bits ignored by the panel), so it needs 3 bytes
// per pixel instead of RGB565's 2.
typedef enum {
    ST7789_COLOR_RGB565 = 0, // 16 bit/pixel, 2 bytes/pixel
    ST7789_COLOR_RGB666 = 1, // 18 bit/pixel, 3 bytes/pixel
} st7789_color_format_t;

// Bring up PIO/DMA/GPIO, run the ST7789 init sequence and configure the
// addressing window for the given panel size/rotation. `round` selects the
// row offset used by the round variant of the 240x240 panel. `color_format`
// selects the COLMOD pixel format and the byte width used by st7789_update().
void st7789_init(uint16_t width, uint16_t height, st7789_rotation_t rotation, bool round,
                  st7789_color_format_t color_format);

// Release the DMA channel and PIO state machine claimed by st7789_init().
void st7789_deinit(void);

// brightness: 0 (off) .. 255 (full), gamma corrected internally.
void st7789_set_backlight(uint8_t brightness);

// Send a command byte followed by an optional data payload (NULL/0 to skip).
void st7789_command(uint8_t cmd, const uint8_t *data, size_t len);

// ---------------------------------------------------------------------------
// Low level pixel-write primitives. These let a caller stream pixel data in
// several chunks (e.g. a ping-pong pair of buffers being colour-converted on
// the fly) instead of handing over one single fully-prepared frame buffer.
//
// Typical use:
//   st7789_start_write();
//   for each chunk:
//       fill_and_convert(buf[i % 2], chunk_len);   // CPU work
//       st7789_write_dma(buf[i % 2], chunk_len);   // waits only for the
//                                                   // *previous* chunk's DMA
//                                                   // to be handed off, then
//                                                   // returns immediately
//   st7789_end_write();
// ---------------------------------------------------------------------------

// Assert CS, issue RAMWR and switch the bus to data mode. Must be paired
// with st7789_end_write().
void st7789_start_write(void);

// Queue an async DMA transfer of raw pixel bytes. Blocks only until the
// previous st7789_write_dma() transfer has been handed off to DMA; it does
// not wait for the new transfer to complete, so the caller may keep
// preparing the next buffer while this one is still going out over the bus.
void st7789_write_dma(const uint8_t *data, size_t len);

// True while a transfer started by st7789_write_dma() is still in flight.
bool st7789_dma_busy(void);

// Block until the most recent st7789_write_dma() transfer has completely
// drained out through the PIO FIFO onto the bus.
void st7789_dma_wait(void);

// Wait for the outstanding transfer to finish and deassert CS.
void st7789_end_write(void);

// Convenience: start_write + write_dma + end_write for a single buffer.
void st7789_write_pixels(const uint8_t *data, size_t len);

// Write a full frame (bytes per pixel per the COLMOD format chosen at init)
// into the currently configured window. The caller owns the buffer; the
// driver never allocates or stores one.
void st7789_update(const uint8_t *frame, size_t len);

// Configure a rectangular sub-window, in raw panel CASET/RASET coordinates,
// for a partial-screen update. x/y is the top-left corner, w/h the size in
// pixels. If the panel/rotation uses a start offset (e.g. round variant or
// Pico Display packs), add that fixed, build-time-known constant to x/y
// yourself; the driver does not cache or recompute it. Follow with
// st7789_start_write()/st7789_write_pixels()/st7789_update() to stream pixel
// data into it; a subsequent full-screen update requires calling
// st7789_set_window() again (or re-running st7789_init()) to restore the
// whole-panel address range.
void st7789_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

#ifdef __cplusplus
}
#endif

#endif // ST7789_H
