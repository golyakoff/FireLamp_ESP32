#ifndef EVLAMP_SCHEDULER_H_
#define EVLAMP_SCHEDULER_H_

#include "common.h"
#include "schedule.h"

/**
 * Load schedule and start the scheduler task. It evaluates the schedule every second
 * (when the time is known) and sends EVENT_SCHEDULE bus events with schedule_action_t payload.
 * Also corrects system time by RTC once an hour.
 */
esp_err_t scheduler_init();

void scheduler_get(schedule_t *s);

/**
 * Validate, apply and save schedule
 */
esp_err_t scheduler_set(const schedule_t *s);

esp_err_t scheduler_reset();

/**
 * User took control of the lamp: stop the running dawn
 */
void scheduler_cancel_dawn();

#endif /* EVLAMP_SCHEDULER_H_ */
