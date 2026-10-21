#ifndef EVLAMP_INPUT_H_
#define EVLAMP_INPUT_H_

#include "common.h"

// Control elements; codes are used in the BLE protocol and the HTTP API
typedef enum {
    INPUT_MODE_ONE_BUTTON = 0, // one button or TTP223 touch module
    INPUT_MODE_FOUR_BUTTONS,   // Main, Reset, Up, Down
    INPUT_MODE_TOUCH,          // ESP32 built-in touch pad

    INPUT_MODE_MAX
} input_mode_t;

enum {
    INPUT_BTN_MAIN = 0,
    INPUT_BTN_RESET,
    INPUT_BTN_UP,
    INPUT_BTN_DOWN,

    INPUT_BTN_MAX
};

/**
 * Payload of EVENT_BUTTON_* bus events
 */
typedef struct
{
    uint8_t button;   // INPUT_BTN_*
    uint8_t clicks;   // EVENT_BUTTON_CLICKED: number of clicks; HOLD*: clicks made right before holding
    uint32_t hold_ms; // EVENT_BUTTON_HOLD*: time since the button was pressed
} input_event_t;

/**
 * Start polling buttons (or touch pad), pins are fixed in menuconfig. Generates bus events:
 *  - EVENT_BUTTON_CLICKED: after release, once the multi-click timeout has passed
 *    (immediately for buttons without multi-click)
 *  - EVENT_BUTTON_HOLD_START: the button has been held for INPUT_HOLD_MS
 *  - EVENT_BUTTON_HOLD: every INPUT_HOLD_REPEAT_MS while held
 *  - EVENT_BUTTON_HOLD_END: released after holding
 */
/**
 * @param mode Control elements
 * @param button_level Pressed level of Main/Up/Down buttons: 1 - active high (TTP223),
 *                     0 - active low (button to GND); the pull resistor goes to the opposite level
 */
esp_err_t input_init(input_mode_t mode, uint8_t button_level);

/**
 * Control elements in use (the touch pad falls back to one button on chips without it)
 */
input_mode_t input_get_mode();

#endif /* EVLAMP_INPUT_H_ */
