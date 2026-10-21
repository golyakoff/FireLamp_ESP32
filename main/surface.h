#ifndef EVLAMP_SURFACE_H_
#define EVLAMP_SURFACE_H_

#include "common.h"

esp_err_t surface_init();

esp_err_t surface_prepare_effect(size_t effect);

bool surface_is_playing();

esp_err_t surface_pause();

esp_err_t surface_stop();

esp_err_t surface_play();

esp_err_t surface_set_effect(size_t num);

esp_err_t surface_next_effect();

esp_err_t surface_prev_effect();

esp_err_t surface_set_brightness(uint8_t val);

/**
 * Set brightness, optionally without saving it (for automatic changes: dawn, hourly brightness)
 */
esp_err_t surface_set_brightness_ex(uint8_t val, bool save);

esp_err_t surface_increment_brightness(int16_t val);

esp_err_t surface_set_fps(uint8_t val);

#endif /* EVLAMP_SURFACE_H_ */
