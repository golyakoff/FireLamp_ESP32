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
#include "demo.h"
#include "ble_server.h"
#include <esp_ota_ops.h>

static void factory_reset()
{
    ESP_LOGW(TAG, "Resetting all settings to defaults...");
    ESP_ERROR_CHECK(effects_reset());
    ESP_ERROR_CHECK(scheduler_reset());
    ESP_ERROR_CHECK(demo_reset());
    ble_server_forget_bonds();
    ESP_ERROR_CHECK(sys_settings_reset());
    ESP_ERROR_CHECK(vol_settings_reset());
    esp_restart();
}

static void open_pairing_window()
{
    ble_server_open_pairing_window();
    // confirmation blink
    surface_flash(rgb_from_values(0, 0, 96), PAIRING_BLINK_MS);
}

static void process_four_buttons(event_t *e)
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
            if (e->type == EVENT_BUTTON_CLICKED && ie->clicks == PAIRING_CLICKS)
                open_pairing_window();
            else if (e->type == EVENT_BUTTON_CLICKED && ie->clicks == 1 && playing)
                demo_next_effect(1);
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

// one button or touch pad
static void process_one_button(event_t *e)
{
    const input_event_t *ie = (const input_event_t *)e->data;
    bool playing = surface_is_playing();
    static bool brightness_up = false;
    static bool reset_armed = false;

    if (e->type == EVENT_BUTTON_CLICKED && ie->clicks == PAIRING_CLICKS)
    {
        open_pairing_window();
        return;
    }

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
                surface_stop();
            else if (ie->clicks == 2)
                demo_next_effect(1);
            else if (ie->clicks == 3)
                demo_next_effect(-1);
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

static void process_button_event(event_t *e)
{
    if (input_get_mode() == INPUT_MODE_FOUR_BUTTONS)
        process_four_buttons(e);
    else
        process_one_button(e);
}

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
        {
            ESP_LOGI(TAG, "Schedule: dawn started, effect '%s'", a->effect);
            demo_stop();
            surface_set_brightness_ex(a->brightness, false);
            int effect = effect_find(a->effect);
            if (effect >= 0 && (size_t)effect != vol_settings.effect)
                surface_set_effect(effect);
            surface_play();
            break;
        }
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

            case EVENT_DEMO_NEXT:
                demo_on_timer();
                break;

            case EVENT_FACTORY_RESET:
                factory_reset();
                break;

            case EVENT_NETWORK_UP:
                // HTTP API only in the "always on" mode, periodic sessions are short
                if (sys_settings.wifi.mode != WIFI_CLIENT_ALWAYS_ON)
                    break;
                ESP_LOGI(TAG, "Network is up, starting HTTPD...");
                err = webserver_restart();
                if (err != ESP_OK)
                    ESP_LOGW(TAG, "Error starting HTTPD: %d (%s)", err, esp_err_to_name(err));
                break;

            case EVENT_NETWORK_DOWN:
                ESP_LOGI(TAG, "Network is down");
                webserver_stop();
                break;

            default:
                ESP_LOGI(TAG, "Unprocessed event %d", e.type);
        }
    }
}

void app_main()
{
    esp_err_t res;
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

    // Demo mode (resumes if it was on)
    ESP_ERROR_CHECK(demo_init());

    // Initialize input
    ESP_ERROR_CHECK(input_init(sys_settings.hw.input_mode, sys_settings.hw.button_level));

    // Start scheduler
    ESP_ERROR_CHECK(scheduler_init());

    // BLE control
    res = ble_server_init();
    if (res != ESP_OK)
        ESP_LOGE(TAG, "Could not start BLE: %d (%s)", res, esp_err_to_name(res));

    // Try to start WIFI
    res = wifi_init();
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
