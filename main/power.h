#ifndef EVLAMP_POWER_H_
#define EVLAMP_POWER_H_

#include "common.h"

/**
 * Power supply: the current model of the lamp, the protection against brownouts and the
 * calibration of the current limit.
 *
 * Brownout protection: a reboot caused by a brownout (the supply could not hold the voltage)
 * lowers the brightness of all effects by POWER_BROWNOUT_STEP percent, down to POWER_PERCENT_MIN.
 * The app shows it and can put it back.
 *
 * Calibration: the whole matrix lights up white in POWER_CALIB_STEPS steps of brightness,
 * POWER_CALIB_HOLD_MS each. The step is stored (with the LEDs dark) before it is shown: a reboot
 * during a step means the supply did not hold it, the step before is the result. So does the app's
 * "the matrix flickers" (power_calib_fail()). The current limit is set from the result with a
 * margin of POWER_CALIB_MARGIN percent, all steps held: the full white current.
 */

#define POWER_PERCENT_MIN   30
#define POWER_BROWNOUT_STEP 15
#define POWER_CALIB_STEPS   10
#define POWER_CALIB_HOLD_MS 5000
#define POWER_CALIB_MARGIN  85

typedef enum {
    POWER_CALIB_IDLE = 0,
    POWER_CALIB_RUNNING,
    POWER_CALIB_DONE,      // this boot; the result is in power_status_t
    POWER_CALIB_CANCELLED,
} power_calib_state_t;

typedef struct
{
    uint8_t percent;       // brightness kept after brownouts, 100 - full
    bool brownout;         // this boot was a brownout reboot
    uint8_t calib_state;   // power_calib_state_t
    uint8_t calib_step;    // running: the step shown, 1..POWER_CALIB_STEPS; done: the steps held
    uint32_t calib_limit;  // done: the current limit set, mA
} power_status_t;

typedef void (*power_cb_t)();

/**
 * At boot, after the system settings and before the surface: the reset reason finishes a
 * calibration or lowers the brightness after a brownout
 */
esp_err_t power_init();

void power_set_cb(power_cb_t cb);

void power_get_status(power_status_t *s);

/**
 * Brightness kept after brownouts, percent (the app puts it back to 100)
 */
esp_err_t power_set_percent(uint8_t percent);

/**
 * Current of the lamp showing the full white matrix at the brightness (0..255), mA
 */
uint32_t power_white_current(uint8_t brightness);

/**
 * Brightness (x256, can exceed 255) at which the full white matrix takes the current limit
 */
uint32_t power_white_brightness();

esp_err_t power_calib_start();

/**
 * The matrix flickers or shows wrong colors at the current step: the step before is the result
 */
esp_err_t power_calib_fail();

esp_err_t power_calib_cancel();

bool power_calib_running();

#endif /* EVLAMP_POWER_H_ */
