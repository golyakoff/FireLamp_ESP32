#include "common.h"
#include "settings.h"
#include "wifi.h"
#include "bus.h"
#include "effect.h"
#include "surface.h"
#include "input.h"
#include "webserver.h"
#include "systime.h"
#include "scheduler.h"
#include <esp_ota_ops.h>

static void factory_reset()
{
    ESP_LOGW(TAG, "Resetting all settings to defaults...");
    ESP_ERROR_CHECK(effects_reset());
    ESP_ERROR_CHECK(scheduler_reset());
    ESP_ERROR_CHECK(sys_settings_reset());
    ESP_ERROR_CHECK(vol_settings_reset());
    esp_restart();
}

#if CONFIG_FL_INPUT_FOUR_BUTTONS

static void process_button_event(event_t *e)
{
    const input_event_t *ie = (const input_event_t *)e->data;
    bool playing = surface_is_playing();
    static bool handled = false; // the current hold has already triggered an action

    if (e->type == EVENT_BUTTON_HOLD_START)
        handled = false;

    switch (ie->button)
    {
        case INPUT_BTN_RESET:
            if (e->type == EVENT_BUTTON_HOLD && !handled && ie->hold_ms >= RESET_HOLD_MS)
            {
                handled = true;
                factory_reset();
            }
            break;

        case INPUT_BTN_MAIN:
            if (e->type == EVENT_BUTTON_CLICKED && playing)
                surface_next_effect();
            else if (e->type == EVENT_BUTTON_HOLD && !handled && ie->hold_ms >= POWER_HOLD_MS)
            {
                handled = true;
                if (playing)
                    surface_stop();
                else
                    surface_play();
            }
            break;

        case INPUT_BTN_UP:
        case INPUT_BTN_DOWN:
        {
            if (!playing) break;
            int dir = ie->button == INPUT_BTN_UP ? 1 : -1;
            if (e->type == EVENT_BUTTON_CLICKED)
                surface_increment_brightness(dir * CLICK_BRIGHTNESS_STEP);
            else if (e->type == EVENT_BUTTON_HOLD_START || e->type == EVENT_BUTTON_HOLD)
                surface_increment_brightness(dir * HOLD_BRIGHTNESS_STEP);
            break;
        }
    }
}

#else // one button or touch pad

static void process_button_event(event_t *e)
{
    const input_event_t *ie = (const input_event_t *)e->data;
    bool playing = surface_is_playing();
    static bool brightness_up = false;
    static bool reset_armed = false;

    if (!playing)
    {
        if (e->type == EVENT_BUTTON_CLICKED)
            surface_play();
        else if (e->type == EVENT_BUTTON_HOLD_START)
            reset_armed = true;
        else if (e->type == EVENT_BUTTON_HOLD && reset_armed && ie->hold_ms >= RESET_HOLD_OFF_MS)
        {
            reset_armed = false;
            factory_reset();
        }
        return;
    }

    switch (e->type)
    {
        case EVENT_BUTTON_CLICKED:
            if (ie->clicks == 1)
                surface_next_effect();
            else if (ie->clicks == 2)
                surface_stop();
            else
                surface_prev_effect();
            break;

        case EVENT_BUTTON_HOLD_START:
            // alternate direction with every hold, but never start against a limit
            brightness_up = !brightness_up;
            if (vol_settings.brightness == 255)
                brightness_up = false;
            else if (vol_settings.brightness <= BRIGHTNESS_MIN)
                brightness_up = true;
            // fall through
        case EVENT_BUTTON_HOLD:
            surface_increment_brightness(brightness_up ? HOLD_BRIGHTNESS_STEP : -HOLD_BRIGHTNESS_STEP);
            break;

        default:
            break;
    }
}

#endif

static void process_schedule_event(event_t *e)
{
    const schedule_action_t *a = (const schedule_action_t *)e->data;

    switch (a->type)
    {
        case SCHEDULE_ACTION_POWER_ON:
            ESP_LOGI(TAG, "Schedule: power on");
            surface_play();
            break;
        case SCHEDULE_ACTION_POWER_OFF:
            ESP_LOGI(TAG, "Schedule: power off");
            surface_stop();
            break;
        case SCHEDULE_ACTION_DAWN_START:
            ESP_LOGI(TAG, "Schedule: dawn started, effect %d", a->effect);
            surface_set_brightness_ex(a->brightness, false);
            if (a->effect < effects_count && a->effect != vol_settings.effect)
                surface_set_effect(a->effect);
            surface_play();
            break;
        case SCHEDULE_ACTION_BRIGHTNESS:
            if (surface_is_playing())
                surface_set_brightness_ex(a->brightness, false);
            break;
        case SCHEDULE_ACTION_DAWN_END:
            ESP_LOGI(TAG, "Schedule: dawn finished");
            if (surface_is_playing())
                surface_set_brightness(a->brightness);
            break;
    }
}

static void main_loop(void *arg)
{
    event_t e;
    esp_err_t err;

    while (1)
    {
        if (bus_receive_event(&e, 1000) != ESP_OK)
            continue;

        switch (e.type)
        {
            case EVENT_BUTTON_CLICKED:
            case EVENT_BUTTON_HOLD_START:
            case EVENT_BUTTON_HOLD:
            case EVENT_BUTTON_HOLD_END:
                // any manual control stops the dawn
                scheduler_cancel_dawn();
                process_button_event(&e);
                break;

            case EVENT_SCHEDULE:
                process_schedule_event(&e);
                break;

            case EVENT_NETWORK_UP:
                ESP_LOGI(TAG, "Network is up, restarting HTTPD...");
                err = webserver_restart();
                if (err != ESP_OK)
                    ESP_LOGW(TAG, "Error starting HTTPD: %d (%s)", err, esp_err_to_name(err));
                break;

            case EVENT_NETWORK_DOWN:
                ESP_LOGI(TAG, "Network is down");
                break;

            default:
                ESP_LOGI(TAG, "Unprocessed event %d", e.type);
        }
    }
}

void app_main()
{
    ESP_LOGI(TAG, "Starting " APP_NAME);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    // Initialize NVS
    ESP_ERROR_CHECK(settings_init());
    // Load system settings from NVS
    ESP_ERROR_CHECK(sys_settings_load());
    // Load volatile settings
    ESP_ERROR_CHECK(vol_settings_load());
    // Load effect parameters
    ESP_ERROR_CHECK(effects_init());
    // Time zone, RTC
    ESP_ERROR_CHECK(systime_init());
    // Init surface
    ESP_ERROR_CHECK(surface_init());

    // Initialize bus
    ESP_ERROR_CHECK(bus_init());

    // Initialize input
    ESP_ERROR_CHECK(input_init());

    // Start scheduler
    ESP_ERROR_CHECK(scheduler_init());

    // Try to start WIFI
    esp_err_t res = wifi_init();
    if (res != ESP_OK)
        ESP_LOGW(TAG, "Could not start WiFi: %d (%s)", res, esp_err_to_name(res));

    // Create main task
    if (xTaskCreate(main_loop, APP_NAME, MAIN_TASK_STACK_SIZE, NULL, MAIN_TASK_PRIORITY, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Could not create main task");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    // Firmware booted fine, cancel OTA rollback
    esp_ota_mark_app_valid_cancel_rollback();
}
