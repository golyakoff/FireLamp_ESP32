#include "input.h"
#include <esp_timer.h>
#include <driver/gpio.h>
#include "bus.h"

#if SOC_TOUCH_SENSOR_SUPPORTED
#include <driver/touch_sens.h>
#endif

typedef struct
{
    // config
    int gpio;
    uint8_t pressed_level;
    bool multi_click;

    // state
    bool stable;          // debounced state
    bool raw;
    uint8_t raw_count;
    bool holding;
    uint8_t clicks;
    int64_t pressed_at;
    int64_t released_at;
    int64_t last_repeat;
} button_t;

static button_t buttons[INPUT_BTN_MAX] = { 0 };

static input_mode_t mode = INPUT_MODE_ONE_BUTTON;
static size_t count = 1; // buttons in use

////////////////////////////////////////////////////////////////////////////////
/// Touch pad

#if SOC_TOUCH_SENSOR_SUPPORTED

static touch_sensor_handle_t touch = NULL;
static touch_channel_handle_t touch_chan = NULL;
static uint32_t touch_baseline = 0;

static esp_err_t touch_init()
{
    touch_sensor_sample_config_t sample_cfg[TOUCH_SAMPLE_CFG_NUM] = {
        TOUCH_SENSOR_V1_DEFAULT_SAMPLE_CONFIG(5.0, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_1V7)
    };
    touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(TOUCH_SAMPLE_CFG_NUM, sample_cfg);
    CHECK_LOGE(touch_sensor_new_controller(&sens_cfg, &touch), "Could not create touch controller");

    touch_channel_config_t chan_cfg = {
        .abs_active_thresh = { 0 }, // hardware threshold is not used, the pad is polled
        .charge_speed = TOUCH_CHARGE_SPEED_7,
        .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
        .group = TOUCH_CHAN_TRIG_GROUP_BOTH,
    };
    CHECK_LOGE(touch_sensor_new_channel(touch, CONFIG_FL_TOUCH_CHANNEL, &chan_cfg, &touch_chan),
            "Could not create touch channel %d", CONFIG_FL_TOUCH_CHANNEL);

    touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    CHECK(touch_sensor_config_filter(touch, &filter_cfg));

    CHECK(touch_sensor_enable(touch));
    CHECK(touch_sensor_start_continuous_scanning(touch));

    // untouched value; the pad must not be touched at boot
    vTaskDelay(pdMS_TO_TICKS(100));
    CHECK(touch_channel_read_data(touch_chan, TOUCH_CHAN_DATA_TYPE_SMOOTH, &touch_baseline));

    touch_chan_info_t info = { 0 };
    touch_sensor_get_channel_info(touch_chan, &info);
    ESP_LOGI(TAG, "Touch pad T%d (GPIO %d), untouched value %lu, threshold %d%%",
            CONFIG_FL_TOUCH_CHANNEL, info.chan_gpio, touch_baseline, CONFIG_FL_TOUCH_THRESHOLD_PERCENT);

    return ESP_OK;
}

static bool touch_read()
{
    uint32_t val = 0;
    if (touch_channel_read_data(touch_chan, TOUCH_CHAN_DATA_TYPE_SMOOTH, &val) != ESP_OK)
        return false;

    // ESP32 touch value drops when touched
    uint32_t threshold = touch_baseline - touch_baseline * CONFIG_FL_TOUCH_THRESHOLD_PERCENT / 100;
    bool touched = val < threshold;
    // slowly follow temperature/humidity drift while untouched
    if (!touched)
        touch_baseline = touch_baseline - touch_baseline / 64 + val / 64;

    return touched;
}

#endif

////////////////////////////////////////////////////////////////////////////////
/// Sources

static esp_err_t source_init()
{
#if SOC_TOUCH_SENSOR_SUPPORTED
    if (mode == INPUT_MODE_TOUCH)
        return touch_init();
#endif
    for (size_t i = 0; i < count; i++)
    {
        gpio_config_t cfg = {
            .pin_bit_mask = BIT64(buttons[i].gpio),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = buttons[i].pressed_level == 0,
            .pull_down_en = buttons[i].pressed_level == 1,
            .intr_type = GPIO_INTR_DISABLE,
        };
        CHECK_LOGE(gpio_config(&cfg), "Could not configure button GPIO %d", buttons[i].gpio);
    }
    return ESP_OK;
}

static bool source_read(button_t *btn)
{
#if SOC_TOUCH_SENSOR_SUPPORTED
    if (mode == INPUT_MODE_TOUCH)
        return touch_read();
#endif
    return gpio_get_level(btn->gpio) == btn->pressed_level;
}

////////////////////////////////////////////////////////////////////////////////

static void send(event_type_t type, size_t id, uint8_t clicks, uint32_t hold_ms)
{
    input_event_t e = {
        .button = id,
        .clicks = clicks,
        .hold_ms = hold_ms,
    };
    bus_send_event(type, &e, sizeof(e));
}

static void process(size_t id, int64_t now)
{
    button_t *btn = &buttons[id];

    // debounce
    bool raw = source_read(btn);
    if (raw != btn->raw)
    {
        btn->raw = raw;
        btn->raw_count = 1;
    }
    else if (btn->raw_count < INPUT_DEBOUNCE_SAMPLES)
        btn->raw_count++;

    bool changed = btn->raw_count == INPUT_DEBOUNCE_SAMPLES && btn->stable != btn->raw;
    if (changed)
        btn->stable = btn->raw;

    uint32_t held = (now - btn->pressed_at) / 1000;

    if (changed && btn->stable)
    {
        // pressed
        btn->pressed_at = now;
        return;
    }

    if (changed && !btn->stable)
    {
        // released
        if (btn->holding)
        {
            btn->holding = false;
            send(EVENT_BUTTON_HOLD_END, id, btn->clicks, held);
            btn->clicks = 0;
            return;
        }
        btn->clicks++;
        btn->released_at = now;
        if (!btn->multi_click || btn->clicks == INPUT_MAX_CLICKS)
        {
            send(EVENT_BUTTON_CLICKED, id, btn->clicks, 0);
            btn->clicks = 0;
        }
        return;
    }

    if (btn->stable)
    {
        if (!btn->holding && held >= INPUT_HOLD_MS)
        {
            btn->holding = true;
            btn->last_repeat = now;
            send(EVENT_BUTTON_HOLD_START, id, btn->clicks, held);
        }
        else if (btn->holding && (now - btn->last_repeat) / 1000 >= INPUT_HOLD_REPEAT_MS)
        {
            btn->last_repeat = now;
            send(EVENT_BUTTON_HOLD, id, btn->clicks, held);
        }
        return;
    }

    if (btn->clicks && (now - btn->released_at) / 1000 >= INPUT_CLICK_TIMEOUT_MS)
    {
        send(EVENT_BUTTON_CLICKED, id, btn->clicks, 0);
        btn->clicks = 0;
    }
}

static void input_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    while (1)
    {
        int64_t now = esp_timer_get_time();
        for (size_t i = 0; i < count; i++)
            process(i, now);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(INPUT_POLL_MS));
    }
}

////////////////////////////////////////////////////////////////////////////////
/// Main button pin

// Inputs with internal pull resistors, without strapping, flash, PSRAM, USB and UART pins
#if CONFIG_IDF_TARGET_ESP32C3
static const uint8_t main_gpio_candidates[] = { 1, 4, 6, 7 };
#else
static const uint8_t main_gpio_candidates[] = { 4, 13, 14, 18, 19, 23, 25, 26, 27, 32, 33 };
#endif

// the pins used by the other functions of the lamp
static bool gpio_taken(uint8_t gpio)
{
    return gpio == CONFIG_FL_MATRIX_GPIO || gpio == CONFIG_FL_I2C_SDA_GPIO || gpio == CONFIG_FL_I2C_SCL_GPIO
        || gpio == CONFIG_FL_BUTTON_UP_GPIO || gpio == CONFIG_FL_BUTTON_DOWN_GPIO
        || gpio == CONFIG_FL_BUTTON_RESET_GPIO;
}

const uint8_t *input_main_gpios(size_t *n)
{
    static uint8_t list[sizeof(main_gpio_candidates) + 1];
    static size_t list_count = 0;

    if (!list_count)
    {
        bool default_added = false;
        for (size_t i = 0; i < sizeof(main_gpio_candidates); i++)
        {
            uint8_t gpio = main_gpio_candidates[i];
            // the pin of the build is always there, even in a build with other pins
            if (!default_added && CONFIG_FL_BUTTON_MAIN_GPIO <= gpio)
            {
                list[list_count++] = CONFIG_FL_BUTTON_MAIN_GPIO;
                default_added = true;
            }
            if (gpio != CONFIG_FL_BUTTON_MAIN_GPIO && !gpio_taken(gpio))
                list[list_count++] = gpio;
        }
        if (!default_added)
            list[list_count++] = CONFIG_FL_BUTTON_MAIN_GPIO;
    }

    *n = list_count;
    return list;
}

bool input_main_gpio_allowed(uint8_t gpio)
{
    size_t n;
    const uint8_t *list = input_main_gpios(&n);
    for (size_t i = 0; i < n; i++)
        if (list[i] == gpio)
            return true;
    return false;
}

uint8_t input_main_gpio(uint8_t setting)
{
    return setting && input_main_gpio_allowed(setting) ? setting : CONFIG_FL_BUTTON_MAIN_GPIO;
}

////////////////////////////////////////////////////////////////////////////////

esp_err_t input_init(input_mode_t m, uint8_t level, uint8_t main_gpio)
{
    mode = m;
#if !SOC_TOUCH_SENSOR_SUPPORTED
    if (mode == INPUT_MODE_TOUCH)
        mode = INPUT_MODE_ONE_BUTTON;
#endif
    count = mode == INPUT_MODE_FOUR_BUTTONS ? INPUT_BTN_MAX : 1;

    // Main waits for multi-click (pairing gesture and one button gestures),
    // other buttons react immediately as in EvLamp
    buttons[INPUT_BTN_MAIN].gpio = input_main_gpio(main_gpio);
    buttons[INPUT_BTN_MAIN].pressed_level = level ? 1 : 0;
    buttons[INPUT_BTN_MAIN].multi_click = true;
    buttons[INPUT_BTN_RESET].gpio = CONFIG_FL_BUTTON_RESET_GPIO;
    buttons[INPUT_BTN_RESET].pressed_level = CONFIG_FL_BUTTON_RESET_LEVEL;
    buttons[INPUT_BTN_UP].gpio = CONFIG_FL_BUTTON_UP_GPIO;
    buttons[INPUT_BTN_UP].pressed_level = level ? 1 : 0;
    buttons[INPUT_BTN_DOWN].gpio = CONFIG_FL_BUTTON_DOWN_GPIO;
    buttons[INPUT_BTN_DOWN].pressed_level = level ? 1 : 0;

    ESP_LOGI(TAG, "Controls: %s, buttons active %s, main button GPIO %d", mode == INPUT_MODE_FOUR_BUTTONS ? "four buttons"
            : mode == INPUT_MODE_TOUCH ? "touch pad" : "one button", level ? "high" : "low",
            buttons[INPUT_BTN_MAIN].gpio);

    CHECK(source_init());

    if (xTaskCreate(input_task, "input", INPUT_TASK_STACK_SIZE, NULL, INPUT_TASK_PRIORITY, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Could not create input task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

input_mode_t input_get_mode()
{
    return mode;
}
