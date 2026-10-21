#ifndef EVLAMP_DS3231_H_
#define EVLAMP_DS3231_H_

#include "common.h"
#include <time.h>

/**
 * Minimal DS3231 RTC driver. The chip keeps UTC.
 */

/**
 * Probe the chip. Returns ESP_ERR_NOT_FOUND if it does not respond.
 */
esp_err_t ds3231_init();

/**
 * Read time
 *
 * @param[out] utc Time
 * @return ESP_ERR_INVALID_STATE if the oscillator has stopped (battery was removed etc),
 *         the time is not valid then
 */
esp_err_t ds3231_get_time(time_t *utc);

/**
 * Set time and clear the "oscillator stopped" flag
 */
esp_err_t ds3231_set_time(time_t utc);

#endif /* EVLAMP_DS3231_H_ */
