#ifndef EVLAMP_SCHEDULE_H_
#define EVLAMP_SCHEDULE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/**
 * Time based lamp control: power on/off at given time, dawn alarm, hourly brightness.
 * Pure logic without FreeRTOS, see scheduler.c for the task.
 */

#define SCHEDULE_DAWN_MIN_DURATION 1
#define SCHEDULE_DAWN_MAX_DURATION 120
#define SCHEDULE_EFFECT_CODE_LEN   16 // same as EFFECT_CODE_MAX_LEN

typedef struct
{
    uint8_t enabled;
    uint8_t days;   // bit 0 - Monday ... bit 6 - Sunday
    uint8_t hour;   // local time
    uint8_t minute;
} schedule_time_t;

typedef struct
{
    schedule_time_t power_on;
    schedule_time_t power_off;
    struct {
        schedule_time_t at;     // moment when the full brightness is reached
        uint8_t duration;       // minutes of brightness ramp before `at`
        uint8_t brightness;     // final brightness
        char effect[SCHEDULE_EFFECT_CODE_LEN + 1]; // effect code
    } dawn;
    struct {
        uint8_t enabled;
        uint8_t values[24];     // brightness for every hour of the day, local time
    } hourly;
} schedule_t;

typedef enum {
    SCHEDULE_ACTION_POWER_ON = 0,
    SCHEDULE_ACTION_POWER_OFF,
    SCHEDULE_ACTION_DAWN_START, // set effect, set brightness without saving, turn on
    SCHEDULE_ACTION_BRIGHTNESS, // set brightness without saving
    SCHEDULE_ACTION_DAWN_END,   // set and save final brightness
} schedule_action_type_t;

typedef struct
{
    uint8_t type;       // schedule_action_type_t
    uint8_t brightness;
    char effect[SCHEDULE_EFFECT_CODE_LEN + 1]; // SCHEDULE_ACTION_DAWN_START: effect code
} schedule_action_t;

typedef struct
{
    int64_t last_minute;     // events fire once, on the first evaluation within their minute
    time_t dawn_alarm;       // `at` moment of the current (or cancelled) dawn, 0 if none
    bool dawn_active;
    bool dawn_cancelled;
    uint8_t dawn_brightness; // last brightness sent during the dawn
    int8_t hourly_hour;      // hour whose brightness has been applied, -1 if none
} schedule_state_t;

#define SCHEDULE_MAX_ACTIONS 4

void schedule_defaults(schedule_t *s);

/**
 * Check values (not the existence of the dawn effect), returns false if invalid
 */
bool schedule_validate(const schedule_t *s);

void schedule_state_init(schedule_state_t *st);

/**
 * Evaluate schedule at moment `now`
 *
 * @param s Schedule
 * @param st Evaluation state, kept between calls
 * @param now Current time
 * @param playing Lamp is on
 * @param[out] actions Actions to perform, at least SCHEDULE_MAX_ACTIONS items
 * @return Number of actions
 */
size_t schedule_evaluate(const schedule_t *s, schedule_state_t *st, time_t now, bool playing,
        schedule_action_t *actions);

/**
 * User took control (button, app) during the dawn: stop changing brightness
 * until the next dawn
 */
void schedule_cancel_dawn(schedule_state_t *st);

#endif /* EVLAMP_SCHEDULE_H_ */
