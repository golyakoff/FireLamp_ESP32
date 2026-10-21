#ifndef EVLAMP_INPUT_H_
#define EVLAMP_INPUT_H_

#include "common.h"

enum {
    INPUT_BTN_MAIN = 0,
#if CONFIG_FL_INPUT_FOUR_BUTTONS
    INPUT_BTN_RESET,
    INPUT_BTN_UP,
    INPUT_BTN_DOWN,
#endif

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
 * Start polling buttons (or touch pad). Generates bus events:
 *  - EVENT_BUTTON_CLICKED: after release, once the multi-click timeout has passed
 *    (immediately for buttons without multi-click)
 *  - EVENT_BUTTON_HOLD_START: the button has been held for INPUT_HOLD_MS
 *  - EVENT_BUTTON_HOLD: every INPUT_HOLD_REPEAT_MS while held
 *  - EVENT_BUTTON_HOLD_END: released after holding
 */
esp_err_t input_init();

#endif /* EVLAMP_INPUT_H_ */
