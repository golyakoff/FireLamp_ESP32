/**
 * Effect time. Force-included into every effect source (main/CMakeLists.txt), so the effects and
 * the lib8tion beat functions they use (GET_MILLIS) read the effect clock instead of the uptime.
 * The clock advances by one nominal frame period per effect step: an effect with a lower tempo
 * runs in a slower world, and its time based and frame based parts stay in step.
 */
#ifndef __EFFECTS_EFFECT_TIME_H__
#define __EFFECTS_EFFECT_TIME_H__

#include <stdint.h>
// declared before the macro below, which would break its prototype; included again later, it is skipped
#include <esp_timer.h>

int64_t effect_time_us(void);

#define esp_timer_get_time() effect_time_us()

#endif /* __EFFECTS_EFFECT_TIME_H__ */
