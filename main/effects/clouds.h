/**
 * Clouds effect
 */
#ifndef __EFFECTS_CLOUDS_H__
#define __EFFECTS_CLOUDS_H__

#include "effect.h"

extern EFFECT_PARAMS(clouds, 2);

esp_err_t effect_clouds_prepare(framebuffer_t *fb);
esp_err_t effect_clouds_run(framebuffer_t *fb);

#define DESCR_EFFECT_CLOUDS DECL_EFFECT_FULL(clouds, "Clouds")

#endif /* __EFFECTS_CLOUDS_H__ */
