/**
 * Petrick fire: heat map rising over two layers of Perlin noise
 */
#ifndef __EFFECTS_PETRICK_FIRE_H__
#define __EFFECTS_PETRICK_FIRE_H__

#include "effect.h"

extern EFFECT_PARAMS(petrick_fire, 2);

esp_err_t effect_petrick_fire_prepare(framebuffer_t *fb);

esp_err_t effect_petrick_fire_run(framebuffer_t *fb);

#define DESCR_EFFECT_PETRICK_FIRE DECL_EFFECT_FULL(petrick_fire, "Petrick fire")

#endif /* __EFFECTS_PETRICK_FIRE_H__ */
