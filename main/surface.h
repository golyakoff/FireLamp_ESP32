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

/**
 * Firmware update: blank the matrix for the transfer (hold = true), and after a failed or
 * aborted update turn the animation back on if it was playing before (hold = false).
 * The demo mode is not touched: its timer waits while the animation is stopped.
 */
esp_err_t surface_hold_for_update(bool hold);

/**
 * Power calibration: the whole matrix white at `brightness` (0..255, the current limit does not
 * apply) instead of the effect, -1 - the effect again. Turns the animation on.
 */
esp_err_t surface_show_white(int16_t brightness);

/**
 * The current limit or the brightness kept after brownouts changed: recalculate the caps
 */
void surface_update_power();

#endif /* EVLAMP_SURFACE_H_ */
