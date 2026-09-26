/* RP2350 Quake adaptation: bounded I2C transactions keep audio service responsive. */
#include "qwstpad.h"

enum {
    REG_INPUT_PORT0         = 0x00,
    REG_OUTPUT_PORT0        = 0x02,
    REG_POLARITY_PORT0      = 0x04,
    REG_CONFIGURATION_PORT0 = 0x06,
};

// Bit position of each button/LED within the TCA9555's combined 16-bit port value.
static const uint8_t BUTTON_BIT[QWSTPAD_NUM_BUTTONS] = {
    [QWSTPAD_BUTTON_A]     = 0xE,
    [QWSTPAD_BUTTON_B]     = 0xC,
    [QWSTPAD_BUTTON_X]     = 0xF,
    [QWSTPAD_BUTTON_Y]     = 0xD,
    [QWSTPAD_BUTTON_UP]    = 0x1,
    [QWSTPAD_BUTTON_DOWN]  = 0x4,
    [QWSTPAD_BUTTON_LEFT]  = 0x2,
    [QWSTPAD_BUTTON_RIGHT] = 0x3,
    [QWSTPAD_BUTTON_PLUS]  = 0xB,
    [QWSTPAD_BUTTON_MINUS] = 0x5,
};

static const uint8_t LED_BIT[QWSTPAD_NUM_LEDS] = {0x6, 0x7, 0x9, 0xA};

static bool reg_write_u16(qwstpad_t *pad, uint8_t reg, uint16_t value) {
    if (!pad || !pad->i2c) return false;
    uint8_t buf[3] = {reg, (uint8_t)value, (uint8_t)(value >> 8)};
    return i2c_write_timeout_us(pad->i2c, pad->address, buf, sizeof(buf), false, 500) == (int)sizeof(buf);
}

static bool reg_read_u16(qwstpad_t *pad, uint8_t reg, uint16_t *value) {
    if (!pad || !pad->i2c || !value) return false;
    uint8_t data[2];
    if (i2c_write_timeout_us(pad->i2c, pad->address, &reg, 1, true, 500) != 1) {
        return false;
    }
    if (i2c_read_timeout_us(pad->i2c, pad->address, data, sizeof(data), false, 500) != (int)sizeof(data)) {
        return false;
    }
    *value = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return true;
}

static bool update_leds(qwstpad_t *pad) {
    uint16_t output = 0;
    for (int i = 0; i < QWSTPAD_NUM_LEDS; i++) {
        // LEDs are wired active-low on the TCA9555 output pins.
        if (!((pad->led_states >> i) & 1u)) {
            output |= (uint16_t)(1u << LED_BIT[i]);
        }
    }
    return reg_write_u16(pad, REG_OUTPUT_PORT0, output);
}

static uint8_t address_index(uint8_t address) {
    switch (address) {
        case QWSTPAD_ADDRESS_ALT1: return 1;
        case QWSTPAD_ADDRESS_ALT2: return 2;
        case QWSTPAD_ADDRESS_ALT3: return 3;
        default:                   return 0;
    }
}

bool qwstpad_init(qwstpad_t *pad, i2c_inst_t *i2c, uint8_t address, bool show_address) {
    if (!pad || !i2c ||
        (address != QWSTPAD_ADDRESS_DEFAULT && address != QWSTPAD_ADDRESS_ALT1 &&
        address != QWSTPAD_ADDRESS_ALT2 && address != QWSTPAD_ADDRESS_ALT3)) {
        return false;
    }

    pad->i2c = i2c;
    pad->address = address;
    pad->led_states = 0;

    // Configure the TCA9555's 16 pins: buttons as (inverted) inputs, LEDs as outputs.
    bool ok = true;
    ok = reg_write_u16(pad, REG_CONFIGURATION_PORT0, 0xF93F) && ok;
    ok = reg_write_u16(pad, REG_POLARITY_PORT0, 0xF83F) && ok;
    ok = reg_write_u16(pad, REG_OUTPUT_PORT0, 0x06C0) && ok;
    if (!ok) {
        return false;
    }

    if (show_address) {
        return qwstpad_set_leds(pad, (uint8_t)(1u << address_index(address)));
    }
    return true;
}

bool qwstpad_read_buttons(qwstpad_t *pad, uint16_t *buttons) {
    if (!pad || !buttons) return false;
    uint16_t state;
    if (!reg_read_u16(pad, REG_INPUT_PORT0, &state)) {
        return false;
    }

    uint16_t mask = 0;
    for (int i = 0; i < QWSTPAD_NUM_BUTTONS; i++) {
        if (state & (uint16_t)(1u << BUTTON_BIT[i])) {
            mask |= (uint16_t)(1u << i);
        }
    }
    *buttons = mask;
    return true;
}

bool qwstpad_set_leds(qwstpad_t *pad, uint8_t states) {
    if (!pad) return false;
    pad->led_states = states & 0x0Fu;
    return update_leds(pad);
}

bool qwstpad_set_led(qwstpad_t *pad, uint8_t led, bool state) {
    if (!pad || led < 1 || led > QWSTPAD_NUM_LEDS) {
        return false;
    }

    uint8_t bit = (uint8_t)(led - 1);
    if (state) {
        pad->led_states |= (uint8_t)(1u << bit);
    } else {
        pad->led_states &= (uint8_t)~(1u << bit);
    }
    return update_leds(pad);
}

bool qwstpad_clear_leds(qwstpad_t *pad) {
    if (!pad) return false;
    pad->led_states = 0;
    return update_leds(pad);
}
