/**
 * Magma effect
 */
#ifndef __EFFECTS_MAGMA_H__
#define __EFFECTS_MAGMA_H__

#include "effect.h"

extern EFFECT_PARAMS(magma, 3);

esp_err_t effect_magma_prepare(framebuffer_t *fb);
esp_err_t effect_magma_run(framebuffer_t *fb);

#define DESCR_EFFECT_MAGMA DECL_EFFECT_FULL(magma, "Magma")

#endif /* __EFFECTS_MAGMA_H__ */
