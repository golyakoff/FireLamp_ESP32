#include "power.h"
#include <esp_system.h>
#include <esp_timer.h>
#include <hal/brownout_hal.h>
#include "settings.h"
#include "surface.h"
#include "ota.h"

#define CALIB_TASK_STACK_SIZE 4096 // writes the settings: NVS or EEPROM
#define CALIB_TASK_PRIORITY   5
#define CALIB_DARK_MS         300 // the LEDs are dark while the step is stored

// The brownout detector resets the chip before the radio and the flash fail: at the lowest
// threshold a sagging supply left it hung, alive but deaf, with the LEDs lit
#if CONFIG_IDF_TARGET_ESP32
#define BROWNOUT_THRESHOLD 7 // 2.80 V
#else
#define BROWNOUT_THRESHOLD 5 // ESP32-C3: 2.76 V
#endif

#define EV_FAIL   BIT(0)
#define EV_CANCEL BIT(1)

static power_cb_t status_cb = NULL;
static power_status_t status = { .percent = 100 };
static volatile bool running = false;
static EventGroupHandle_t events = NULL;
static bool save_pending = false;

static size_t num_leds()
{
    return sys_settings.leds.width * sys_settings.leds.height;
}

// raw brightness of a calibration step, 1..POWER_CALIB_STEPS
static uint8_t step_brightness(uint8_t step)
{
    return 255 * step / POWER_CALIB_STEPS;
}

static void changed()
{
    if (status_cb)
        status_cb();
}

uint32_t power_white_current(uint8_t brightness)
{
    size_t n = num_leds();
    return CONFIG_FL_CONTROLLER_CURRENT_MA + n * CONFIG_FL_LED_IDLE_CURRENT_UA / 1000
        + n * CONFIG_FL_SINGLE_LED_CURRENT_MA * brightness / 255;
}

uint32_t power_white_brightness()
{
    // the current limit is of the whole lamp: the controller and the unlit LEDs take their part
    size_t n = num_leds();
    int leds_ma = (int)sys_settings.leds.current_limit - CONFIG_FL_CONTROLLER_CURRENT_MA
            - (int)(n * CONFIG_FL_LED_IDLE_CURRENT_UA / 1000);
    if (leds_ma < 0 || !n)
        return 0;
    return (uint32_t)((float)leds_ma / n / CONFIG_FL_SINGLE_LED_CURRENT_MA * 255.0f * 256.0f);
}

// The supply held `held` steps: the current limit from the last of them with a margin, or the full
// white current when it held all of them. Lower than one step: half of the first one. Not saved.
static void apply_result(uint8_t held)
{
    uint32_t limit;
    if (held >= POWER_CALIB_STEPS)
        limit = power_white_current(255);
    else
    {
        uint8_t b = held ? step_brightness(held) : step_brightness(1) / 2;
        limit = power_white_current((uint32_t)b * POWER_CALIB_MARGIN / 100);
    }
    sys_settings.leds.current_limit = limit;
    sys_settings.power.percent = 100;
    sys_settings.power.calib_step = 0;

    status.percent = 100;
    status.calib_state = POWER_CALIB_DONE;
    status.calib_step = held;
    status.calib_limit = limit;
    ESP_LOGW(TAG, "Power calibration: the supply held %d of %d steps, current limit %lu mA",
            held, POWER_CALIB_STEPS, limit);
}

esp_err_t power_init()
{
    events = xEventGroupCreate();
    if (!events)
        return ESP_ERR_NO_MEM;

    esp_reset_reason_t reason = esp_reset_reason();
    status.brownout = reason == ESP_RST_BROWNOUT;
    status.percent = sys_settings.power.percent;

    uint8_t step = sys_settings.power.calib_step;
    if (step)
    {
        // our own reboot (an app command) only cancels; any other reset during a step - a
        // brownout, a crash, the power gone - means the supply did not hold it
        if (reason == ESP_RST_SW)
        {
            sys_settings.power.calib_step = 0;
            status.calib_state = POWER_CALIB_CANCELLED;
            ESP_LOGW(TAG, "Power calibration cancelled by a reboot");
        }
        else
            apply_result(step - 1);
        save_pending = true;
        status.brownout = false;
        return ESP_OK;
    }

    if (status.brownout)
    {
        int percent = sys_settings.power.percent - POWER_BROWNOUT_STEP;
        sys_settings.power.percent = percent < POWER_PERCENT_MIN ? POWER_PERCENT_MIN : percent;
        save_pending = true;
        status.percent = sys_settings.power.percent;
        ESP_LOGW(TAG, "Rebooted by a brownout: the brightness is lowered to %d %%", status.percent);
    }

    return ESP_OK;
}

void power_start()
{
    // the LEDs may still show the frame from before the reset (they keep it while powered): the
    // flash is written once the surface has darkened them, and the detector watches from now on
    if (save_pending)
    {
        sys_settings_save();
        save_pending = false;
    }
    brownout_hal_config_t cfg = {
        .threshold = BROWNOUT_THRESHOLD,
        .enabled = true,
        .reset_enabled = true,
        .flash_power_down = true,
        .rf_power_down = true,
    };
    brownout_hal_config(&cfg);
}

void power_set_cb(power_cb_t cb)
{
    status_cb = cb;
}

void power_get_status(power_status_t *s)
{
    *s = status;
}

esp_err_t power_set_percent(uint8_t percent)
{
    CHECK_ARG(percent >= POWER_PERCENT_MIN && percent <= 100);
    sys_settings.power.percent = percent;
    CHECK(sys_settings_save());
    status.percent = percent;
    surface_update_power();
    changed();
    return ESP_OK;
}

bool power_calib_running()
{
    return running;
}

static void calib_task(void *arg)
{
    uint8_t held = POWER_CALIB_STEPS;
    bool cancelled = false;

    for (uint8_t step = 1; step <= POWER_CALIB_STEPS; step++)
    {
        // stored with the LEDs dark: writing the flash during a brownout could spoil it
        surface_show_white(0);
        vTaskDelay(pdMS_TO_TICKS(CALIB_DARK_MS));
        sys_settings.power.calib_step = step;
        sys_settings_save();

        status.calib_step = step;
        changed();
        surface_show_white(step_brightness(step));

        EventBits_t ev = xEventGroupWaitBits(events, EV_FAIL | EV_CANCEL, pdTRUE, pdFALSE,
                pdMS_TO_TICKS(POWER_CALIB_HOLD_MS));
        if (ev & EV_CANCEL)
        {
            cancelled = true;
            break;
        }
        if (ev & EV_FAIL)
        {
            held = step - 1;
            break;
        }
    }

    surface_show_white(-1);
    if (cancelled)
    {
        sys_settings.power.calib_step = 0;
        sys_settings_save();
        status.calib_state = POWER_CALIB_CANCELLED;
        ESP_LOGW(TAG, "Power calibration cancelled");
    }
    else
    {
        apply_result(held);
        sys_settings_save();
        surface_update_power();
    }
    running = false;
    changed();
    vTaskDelete(NULL);
}

esp_err_t power_calib_start()
{
    if (running || ota_in_progress())
        return ESP_ERR_INVALID_STATE;

    xEventGroupClearBits(events, EV_FAIL | EV_CANCEL);
    running = true;
    status.calib_state = POWER_CALIB_RUNNING;
    status.calib_step = 0;
    status.calib_limit = 0;
    if (xTaskCreate(calib_task, "calib", CALIB_TASK_STACK_SIZE, NULL, CALIB_TASK_PRIORITY, NULL) != pdPASS)
    {
        running = false;
        status.calib_state = POWER_CALIB_IDLE;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGW(TAG, "Power calibration started");
    changed();
    return ESP_OK;
}

esp_err_t power_calib_fail()
{
    if (!running)
        return ESP_ERR_INVALID_STATE;
    xEventGroupSetBits(events, EV_FAIL);
    return ESP_OK;
}

esp_err_t power_calib_cancel()
{
    if (!running)
        return ESP_ERR_INVALID_STATE;
    xEventGroupSetBits(events, EV_CANCEL);
    return ESP_OK;
}
