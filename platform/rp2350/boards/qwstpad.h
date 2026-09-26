#ifndef QWSTPAD_H
#define QWSTPAD_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pimoroni QwSTPad (TCA9555-based I2C gamepad) driver for the Pico SDK's
// hardware I2C peripheral. The caller owns and configures the I2C bus
// (i2c_init(), gpio_set_function()/pull-ups) since it may be shared with
// other devices; only the pad's own address is handled here.

#define QWSTPAD_NUM_LEDS    4
#define QWSTPAD_NUM_BUTTONS 10

#define QWSTPAD_ADDRESS_DEFAULT 0x21
#define QWSTPAD_ADDRESS_ALT1    0x23
#define QWSTPAD_ADDRESS_ALT2    0x25
#define QWSTPAD_ADDRESS_ALT3    0x27

// Bit position of each button within the mask returned by qwstpad_read_buttons().
typedef enum {
    QWSTPAD_BUTTON_A     = 0,
    QWSTPAD_BUTTON_B     = 1,
    QWSTPAD_BUTTON_X     = 2,
    QWSTPAD_BUTTON_Y     = 3,
    QWSTPAD_BUTTON_UP    = 4,
    QWSTPAD_BUTTON_DOWN  = 5,
    QWSTPAD_BUTTON_LEFT  = 6,
    QWSTPAD_BUTTON_RIGHT = 7,
    QWSTPAD_BUTTON_PLUS  = 8,
    QWSTPAD_BUTTON_MINUS = 9,
} qwstpad_button_t;

// Minimal per-device state: the I2C handle/address plus the last LED bits
// written (needed so qwstpad_set_led()/qwstpad_clear_leds() can change one
// LED without disturbing the others, without re-reading the chip).
typedef struct {
    i2c_inst_t *i2c;
    uint8_t address;
    uint8_t led_states;
} qwstpad_t;

// Configures the TCA9555's I/O directions/polarity for the pad's buttons and
// LEDs. `address` must be one of the QWSTPAD_ADDRESS_* values. If
// `show_address` is true the LEDs briefly show which of the 4 addresses this
// pad is set to. Returns false if the address is invalid or an I2C transfer fails.
bool qwstpad_init(qwstpad_t *pad, i2c_inst_t *i2c, uint8_t address, bool show_address);

// Reads all 10 buttons in one I2C transaction. On success, *buttons is a
// bitmask with bit (1u << QWSTPAD_BUTTON_xxx) set for each pressed button.
// Returns false (leaving *buttons untouched) on I2C failure.
bool qwstpad_read_buttons(qwstpad_t *pad, uint16_t *buttons);

static inline bool qwstpad_button_pressed(uint16_t buttons, qwstpad_button_t button) {
    return (buttons & (1u << button)) != 0;
}

// states: bit 0..3 select LED 1..4 (1 = on).
bool qwstpad_set_leds(qwstpad_t *pad, uint8_t states);

// led: 1..4.
bool qwstpad_set_led(qwstpad_t *pad, uint8_t led, bool state);

bool qwstpad_clear_leds(qwstpad_t *pad);

#ifdef __cplusplus
}
#endif

#endif // QWSTPAD_H
