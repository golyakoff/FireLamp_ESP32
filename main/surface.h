#ifndef EVLAMP_SURFACE_H_
#define EVLAMP_SURFACE_H_

#include "common.h"
#include <color.h>

esp_err_t surface_init();

esp_err_t surface_prepare_effect(size_t effect);

bool surface_is_playing();

esp_err_t surface_pause();

esp_err_t surface_stop();

esp_err_t surface_play();

esp_err_t surface_set_effect(size_t num);

/**
 * Set effect, optionally without saving it as the current effect (demo mode)
 */
esp_err_t surface_set_effect_ex(size_t num, bool save);

esp_err_t surface_next_effect();

esp_err_t surface_prev_effect();

esp_err_t surface_set_brightness(uint8_t val);

/**
 * Set brightness, optionally without saving it (for automatic changes: dawn, hourly brightness)
 */
esp_err_t surface_set_brightness_ex(uint8_t val, bool save);

esp_err_t surface_increment_brightness(int16_t val);

esp_err_t surface_set_fps(uint8_t val);

typedef void (*surface_state_cb_t)();

/**
 * Callback called when the lamp state changes: on/off, effect, brightness, FPS, demo mode.
 * Called from different tasks, must be fast.
 */
void surface_set_state_cb(surface_state_cb_t cb);

/**
 * Report a state change to the callback
 */
void surface_state_changed();

/**
 * Show a solid color for `ms` milliseconds (confirmation blink).
 * Blocks when the lamp is off.
 */
esp_err_t surface_flash(rgb_t color, uint32_t ms);

#endif /* EVLAMP_SURFACE_H_ */
