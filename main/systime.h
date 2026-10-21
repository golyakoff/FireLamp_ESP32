#ifndef EVLAMP_SYSTIME_H_
#define EVLAMP_SYSTIME_H_

#include "common.h"
#include <time.h>

/**
 * System time: sources, time zone, optional DS3231 RTC
 */

typedef enum {
    TIME_SOURCE_NONE = 0, // time is unknown
    TIME_SOURCE_RTC,
    TIME_SOURCE_BLE,
    TIME_SOURCE_SNTP,
    TIME_SOURCE_API,
} time_source_t;

#define TZ_MAX_LEN 48

/**
 * Apply time zone from settings, read time from RTC if it is present
 */
esp_err_t systime_init();

/**
 * True when time was set by any source since boot
 */
bool systime_is_valid();

time_source_t systime_source();

const char *systime_source_name(time_source_t source);

bool systime_has_rtc();

/**
 * Set system time (and RTC if present)
 */
esp_err_t systime_set(time_t utc, time_source_t source);

/**
 * Set POSIX time zone string, e.g. "MSK-3" or "CET-1CEST,M3.5.0,M10.5.0/3", and save it
 */
esp_err_t systime_set_tz(const char *tz);

const char *systime_get_tz();

/**
 * Correct system time drift by RTC (when RTC is present and valid)
 */
esp_err_t systime_sync_from_rtc();

#endif /* EVLAMP_SYSTIME_H_ */
