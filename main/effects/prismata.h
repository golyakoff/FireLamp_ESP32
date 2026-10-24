/**
 * Prismata effect
 */
#ifndef __EFFECTS_PRISMATA_H__
#define __EFFECTS_PRISMATA_H__

#include "effect.h"

extern EFFECT_PARAMS(prismata, 3);

esp_err_t effect_prismata_run(framebuffer_t *fb);

#define DESCR_EFFECT_PRISMATA DECL_EFFECT_SHORT(prismata, "Prismata")

#endif /* __EFFECTS_PRISMATA_H__ */
