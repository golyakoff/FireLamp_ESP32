/**
 * Butterflies effect
 */
#ifndef __EFFECTS_BUTTERFLIES_H__
#define __EFFECTS_BUTTERFLIES_H__

#include "effect.h"

extern EFFECT_PARAMS(butterflies, 6);

esp_err_t effect_butterflies_prepare(framebuffer_t *fb);

esp_err_t effect_butterflies_run(framebuffer_t *fb);

#define DESCR_EFFECT_BUTTERFLIES DECL_EFFECT_FULL(butterflies, "Butterflies")

#endif /* __EFFECTS_BUTTERFLIES_H__ */
