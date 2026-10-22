/**
 * Fireflies with trails effect
 */
#ifndef __EFFECTS_TRAIL_FIREFLIES_H__
#define __EFFECTS_TRAIL_FIREFLIES_H__

#include "effect.h"

extern EFFECT_PARAMS(trail_fireflies, 3);

esp_err_t effect_trail_fireflies_prepare(framebuffer_t *fb);

esp_err_t effect_trail_fireflies_run(framebuffer_t *fb);

#define DESCR_EFFECT_TRAIL_FIREFLIES DECL_EFFECT_FULL(trail_fireflies, "Fireflies with trails")

#endif /* __EFFECTS_TRAIL_FIREFLIES_H__ */
