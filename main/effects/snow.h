/**
 * Snowfall effect
 */
#ifndef __EFFECTS_SNOW_H__
#define __EFFECTS_SNOW_H__

#include "effect.h"

extern EFFECT_PARAMS(snow, 2);

esp_err_t effect_snow_run(framebuffer_t *fb);

#define DESCR_EFFECT_SNOW DECL_EFFECT_SHORT(snow, "Snowfall")

#endif /* __EFFECTS_SNOW_H__ */
