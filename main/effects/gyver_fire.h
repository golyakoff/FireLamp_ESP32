/**
 * Gyver fire: the classic fire of GyverLamp, flame from brightness and hue masks with sparks
 */
#ifndef __EFFECTS_GYVER_FIRE_H__
#define __EFFECTS_GYVER_FIRE_H__

#include "effect.h"

extern EFFECT_PARAMS(gyver_fire, 4);

esp_err_t effect_gyver_fire_prepare(framebuffer_t *fb);

esp_err_t effect_gyver_fire_run(framebuffer_t *fb);

#define DESCR_EFFECT_GYVER_FIRE DECL_EFFECT_FULL(gyver_fire, "Gyver fire")

#endif /* __EFFECTS_GYVER_FIRE_H__ */
