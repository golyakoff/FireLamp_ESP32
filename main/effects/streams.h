/**
 * Noise streams effects: rainbow snake, soaring fire
 */
#ifndef __EFFECTS_STREAMS_H__
#define __EFFECTS_STREAMS_H__

#include "effect.h"

extern EFFECT_PARAMS(rainbow_snake, 3);
extern EFFECT_PARAMS(soaring_fire, 2);

esp_err_t effect_rainbow_snake_prepare(framebuffer_t *fb);
esp_err_t effect_rainbow_snake_run(framebuffer_t *fb);

esp_err_t effect_soaring_fire_prepare(framebuffer_t *fb);
esp_err_t effect_soaring_fire_run(framebuffer_t *fb);

#define DESCR_EFFECT_RAINBOW_SNAKE DECL_EFFECT_FULL(rainbow_snake, "Rainbow snake")
#define DESCR_EFFECT_SOARING_FIRE DECL_EFFECT_FULL(soaring_fire, "Soaring fire")

#endif /* __EFFECTS_STREAMS_H__ */
