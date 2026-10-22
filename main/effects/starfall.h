/**
 * Starfall / snowstorm effect
 */
#ifndef __EFFECTS_STARFALL_H__
#define __EFFECTS_STARFALL_H__

#include "effect.h"

extern EFFECT_PARAMS(starfall, 5);

esp_err_t effect_starfall_run(framebuffer_t *fb);

#define DESCR_EFFECT_STARFALL DECL_EFFECT_SHORT(starfall, "Starfall")

#endif /* __EFFECTS_STARFALL_H__ */
