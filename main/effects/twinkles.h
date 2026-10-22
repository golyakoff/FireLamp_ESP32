/**
 * Twinkles effect
 */
#ifndef __EFFECTS_TWINKLES_H__
#define __EFFECTS_TWINKLES_H__

#include "effect.h"

extern EFFECT_PARAMS(twinkles, 3);

esp_err_t effect_twinkles_prepare(framebuffer_t *fb);

esp_err_t effect_twinkles_run(framebuffer_t *fb);

#define DESCR_EFFECT_TWINKLES DECL_EFFECT_FULL(twinkles, "Twinkles")

#endif /* __EFFECTS_TWINKLES_H__ */
