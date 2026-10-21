#include "scheduler.h"
#include "storage.h"
#include "systime.h"
#include "surface.h"
#include "bus.h"

_Static_assert(sizeof(schedule_t) <= STORAGE_SCHEDULE_MAX_SIZE, "Schedule does not fit storage record");

static schedule_t schedule;
static schedule_state_t state;
static SemaphoreHandle_t lock = NULL;

static void scheduler_task(void *arg)
{
    schedule_action_t actions[SCHEDULE_MAX_ACTIONS];
    TickType_t last_wake = xTaskGetTickCount();
    time_t last_rtc_sync = time(NULL);

    while (1)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));

        time_t now = time(NULL);
        if (systime_has_rtc() && now - last_rtc_sync >= RTC_SYNC_INTERVAL_S)
        {
            systime_sync_from_rtc();
            last_rtc_sync = now = time(NULL);
        }

        if (!systime_is_valid())
            continue;

        xSemaphoreTake(lock, portMAX_DELAY);
        size_t n = schedule_evaluate(&schedule, &state, now, surface_is_playing(), actions);
        xSemaphoreGive(lock);

        for (size_t i = 0; i < n; i++)
            bus_send_event(EVENT_SCHEDULE, &actions[i], sizeof(schedule_action_t));
    }
}

esp_err_t scheduler_init()
{
    lock = xSemaphoreCreateMutex();
    if (!lock)
        return ESP_ERR_NO_MEM;

    size_t size = sizeof(schedule);
    if (storage_load(STORAGE_SCHEDULE, &schedule, &size) != ESP_OK
            || size != sizeof(schedule) || !schedule_validate(&schedule))
    {
        ESP_LOGW(TAG, "No valid schedule stored, using defaults");
        CHECK(scheduler_reset());
    }
    schedule_state_init(&state);

    if (xTaskCreate(scheduler_task, "scheduler", SCHEDULER_TASK_STACK_SIZE, NULL, SCHEDULER_TASK_PRIORITY, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Could not create scheduler task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void scheduler_get(schedule_t *s)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    *s = schedule;
    xSemaphoreGive(lock);
}

esp_err_t scheduler_set(const schedule_t *s)
{
    CHECK_ARG(s && schedule_validate(s));

    xSemaphoreTake(lock, portMAX_DELAY);
    schedule = *s;
    // re-evaluate from scratch: e.g. new hourly values apply right away
    schedule_state_init(&state);
    xSemaphoreGive(lock);

    ESP_LOGI(TAG, "Schedule updated");

    return storage_save(STORAGE_SCHEDULE, s, sizeof(schedule_t));
}

esp_err_t scheduler_reset()
{
    schedule_t s;
    schedule_defaults(&s);

    if (lock) xSemaphoreTake(lock, portMAX_DELAY);
    schedule = s;
    if (lock) xSemaphoreGive(lock);

    return storage_save(STORAGE_SCHEDULE, &s, sizeof(schedule_t));
}

void scheduler_cancel_dawn()
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (state.dawn_active)
        ESP_LOGI(TAG, "Dawn cancelled by user");
    schedule_cancel_dawn(&state);
    xSemaphoreGive(lock);
}
