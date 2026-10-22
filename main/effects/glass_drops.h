/**
 * Drops on glass effect
 */
#ifndef __EFFECTS_GLASS_DROPS_H__
#define __EFFECTS_GLASS_DROPS_H__

#include "effect.h"

extern EFFECT_PARAMS(glass_drops, 3);

esp_err_t effect_glass_drops_prepare(framebuffer_t *fb);

esp_err_t effect_glass_drops_run(framebuffer_t *fb);

#define DESCR_EFFECT_GLASS_DROPS DECL_EFFECT_FULL(glass_drops, "Drops on glass")

#endif /* __EFFECTS_GLASS_DROPS_H__ */
