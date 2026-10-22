/**
 * Thunderstorm effect: rain with clouds and lightning
 */
#ifndef __EFFECTS_STORM_H__
#define __EFFECTS_STORM_H__

#include "effect.h"

extern EFFECT_PARAMS(storm, 4);

esp_err_t effect_storm_prepare(framebuffer_t *fb);

esp_err_t effect_storm_run(framebuffer_t *fb);

#define DESCR_EFFECT_STORM DECL_EFFECT_FULL(storm, "Thunderstorm")

#endif /* __EFFECTS_STORM_H__ */
