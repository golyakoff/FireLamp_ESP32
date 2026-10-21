#ifndef EVLAMP_DEMO_H_
#define EVLAMP_DEMO_H_

#include "common.h"
#include "demo_core.h"

/**
 * Demo mode: plays included effects one after another.
 * Effect switches made by the demo are not saved to the settings storage.
 */

#define DEMO_MIN_DURATION 10
#define DEMO_MAX_DURATION 3600
#define DEMO_MAX_RANDOM   3600

typedef struct
{
    uint8_t order;      // demo_order_t
    uint16_t duration;  // seconds, DEMO_MIN_DURATION..DEMO_MAX_DURATION
    uint16_t random;    // max random addition, seconds, 0..DEMO_MAX_RANDOM
} demo_settings_t;

/**
 * Load settings, resume the demo if it was on before reboot
 */
esp_err_t demo_init();

void demo_get_settings(demo_settings_t *s);

esp_err_t demo_set_settings(const demo_settings_t *s);

bool demo_is_included(size_t effect);

/**
 * Include or exclude effect. Excluding the last included effect fails with ESP_ERR_INVALID_STATE.
 */
esp_err_t demo_set_included(size_t effect, bool included);

esp_err_t demo_reset();

bool demo_is_active();

esp_err_t demo_start();

void demo_stop();

/**
 * Switch to the next (dir = 1) or previous (dir = -1) included effect now and restart the timer
 */
esp_err_t demo_skip(int dir);

/**
 * Manual "next/previous effect" (buttons, app): in the demo mode switches to the next
 * included effect and the demo continues, otherwise just the next effect in the list
 */
esp_err_t demo_next_effect(int dir);

/**
 * Timer expired (EVENT_DEMO_NEXT): switch to the next effect
 */
esp_err_t demo_on_timer();

#endif /* EVLAMP_DEMO_H_ */
