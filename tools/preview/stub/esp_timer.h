// Host stub for tools/preview: just enough of the firmware headers to run the effects on a PC
#pragma once
#include <stdint.h>
extern int64_t sim_time_us;
static inline int64_t esp_timer_get_time(void) { return sim_time_us; }
