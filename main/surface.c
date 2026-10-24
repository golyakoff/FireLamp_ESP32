#include "surface.h"
#include <framebuffer.h>
#include <led_strip.h>
#include <lib8tion.h>
#include "settings.h"
#include "effect.h"
#include "power.h"
#include <esp_timer.h>

#define FRAME_TICKS (pdMS_TO_TICKS(1000 / vol_settings.fps))
#define BIT_READY   BIT(0)
#define BIT_PLAYING BIT(1)

static led_strip_handle_t strip = NULL;
static bool strip_busy = false;
static uint16_t *led_map = NULL; // framebuffer pixel offset -> LED index in the strip
// brightness at which the full white matrix takes the current left for the LEDs, x256 (it can
// exceed 255); every effect is capped by its own peak: brightness_cap()
static uint32_t white_brightness = 255 * 256;
// power calibration: the whole matrix white at this brightness, -1 - the effect
static volatile int16_t calib_white = -1;
static size_t num_leds, fb_width, fb_height;
static framebuffer_t framebuffer;
static EventGroupHandle_t state;
// Serializes control from different tasks: main loop, BLE, HTTP API
static SemaphoreHandle_t lock = NULL;
static surface_state_cb_t state_cb = NULL;
static volatile int64_t flash_until = 0;
static rgb_t flash_color;

// Effect clock (effects/effect_time.h): one nominal frame period per effect step
static volatile int64_t effect_us = 0;
// Tempo: an effect step every time the accumulator passes EFFECT_TEMPO_MAX; the output frames
// in between blend the frames before and after the last step
static uint32_t tempo_acc = 0;
static rgb_t *prev_frame = NULL, *blend_frame = NULL;
// the next frame starts the effect afresh: no blending with what was shown before
static volatile bool blend_restart = true;

static const led_model_t led_models[LED_TYPE_MAX] = {
    [LED_TYPE_WS2812] = LED_MODEL_WS2812,
    [LED_TYPE_SK6812] = LED_MODEL_SK6812,
    [LED_TYPE_WS2811] = LED_MODEL_WS2811,
};

// The LEDs are connected row by row starting from the bottom left corner: like a snake
// (serpentine) or every row from the left (progressive). Other layouts are rotation + mirror.
// Framebuffer y = 0 is the bottom row.
static void build_led_map()
{
    size_t w = sys_settings.leds.width;

    for (size_t y = 0; y < fb_height; y++)
        for (size_t x = 0; x < fb_width; x++)
        {
            size_t px, py;
            switch (sys_settings.leds.rotation)
            {
                case MATRIX_ROTATION_90:
                    px = y;
                    py = fb_width - 1 - x;
                    break;
                case MATRIX_ROTATION_180:
                    px = fb_width - 1 - x;
                    py = fb_height - 1 - y;
                    break;
                case MATRIX_ROTATION_270:
                    px = fb_height - 1 - y;
                    py = x;
                    break;
                default:
                    px = x;
                    py = y;
            }
            if (sys_settings.leds.mirror)
                px = w - 1 - px;

            bool reversed = sys_settings.hw.wiring == MATRIX_WIRING_SERPENTINE && py % 2;
            led_map[y * fb_width + x] = py * w + (reversed ? w - px - 1 : px);
        }
}

static esp_err_t strip_init()
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_FL_MATRIX_GPIO,
        .max_leds = num_leds,
        .led_model = sys_settings.leds.type < LED_TYPE_MAX
            ? led_models[sys_settings.leds.type]
            : LED_MODEL_WS2812,
        .color_component_format = sys_settings.leds.color_order == LED_COLOR_ORDER_RGB
            ? LED_STRIP_COLOR_COMPONENT_FMT_RGB
            : LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };

#if CONFIG_FL_LED_BACKEND_SPI
    led_strip_spi_config_t spi_config = {
        .clk_src = SPI_CLK_SRC_DEFAULT,
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    CHECK_LOGE(led_strip_new_spi_device(&strip_config, &spi_config, &strip),
            "Could not create SPI LED strip");
#else
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 256, // 4 memory blocks: fewer refill interrupts
    };
    CHECK_LOGE(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip),
            "Could not create RMT LED strip");
#endif

    return ESP_OK;
}

// Maximal brightness of the effect: its brightest frame takes the current left for the LEDs
static uint8_t brightness_cap(size_t effect)
{
    // the brightness kept after brownout reboots applies on top
    uint64_t cap = (uint64_t)white_brightness * 100 / effect_peak(effect) * sys_settings.power.percent / 100 / 256;
    return cap > 255 ? 255 : cap < 1 ? 1 : cap;
}

// frame renderer, framebuffer -> LED strip
int64_t effect_time_us(void)
{
    return effect_us;
}

// arg: the pixels to show instead of fb->data (a blended frame), or NULL
static esp_err_t render_frame(framebuffer_t *fb, void *arg)
{
    const rgb_t *data = arg ? (const rgb_t *)arg : fb->data;

    // previous frame is sent by DMA/RMT while the effect computes the next one
    if (strip_busy)
    {
        CHECK(led_strip_refresh_async_done(strip));
        strip_busy = false;
    }

    int16_t white = calib_white;
    if (esp_timer_get_time() < flash_until)
    {
        for (size_t i = 0; i < num_leds; i++)
            CHECK(led_strip_set_pixel(strip, i, flash_color.r, flash_color.g, flash_color.b));
    }
    else if (white >= 0)
    {
        // the current limit does not apply: the calibration finds it
        for (size_t i = 0; i < num_leds; i++)
            CHECK(led_strip_set_pixel(strip, i, white, white, white));
    }
    else
    {
        uint8_t brightness = scale8_video(vol_settings.brightness, brightness_cap(vol_settings.effect));
        for (size_t i = 0; i < num_leds; i++)
        {
            rgb_t c = rgb_scale_video(data[i], brightness);
            CHECK(led_strip_set_pixel(strip, led_map[i], c.r, c.g, c.b));
        }
    }

    CHECK(led_strip_refresh_async(strip));
    strip_busy = true;

    return ESP_OK;
}

#if CONFIG_FL_EFFECTS_BENCHMARK

#define BENCHMARK_FRAMES 100

static void benchmark()
{
    ESP_LOGI(TAG, "Effects benchmark, %d frames each, %dx%d, frame budget at %d fps: %lu us",
        BENCHMARK_FRAMES, fb_width, fb_height, CONFIG_FL_FPS_DEFAULT, 1000000UL / CONFIG_FL_FPS_DEFAULT);

    uint64_t output_total = 0;
    for (size_t e = 0; e < effects_count; e++)
    {
        if (effects[e].prepare && effects[e].prepare(&framebuffer) != ESP_OK)
        {
            ESP_LOGW(TAG, "%2d %-16s: prepare failed", e, effects[e].name);
            continue;
        }

        uint64_t total = 0, max = 0;
        for (size_t i = 0; i < BENCHMARK_FRAMES; i++)
        {
            int64_t t0 = esp_timer_get_time();
            effects[e].run(&framebuffer);
            int64_t t1 = esp_timer_get_time();
            // waits for the previous frame transfer, so it is measured separately from the effect
            fb_render(&framebuffer, NULL);
            output_total += esp_timer_get_time() - t1;

            uint64_t dt = t1 - t0;
            total += dt;
            if (dt > max) max = dt;
            vTaskDelay(1); // let the idle task feed the watchdog
        }
        ESP_LOGI(TAG, "%2d %-16s: avg %6llu us, max %6llu us", e, effects[e].name,
            total / BENCHMARK_FRAMES, max);
    }
    ESP_LOGI(TAG, "Output (pixel packing + waiting for previous transfer): avg %llu us",
        output_total / (BENCHMARK_FRAMES * effects_count));
    ESP_LOGI(TAG, "Free heap: %lu bytes, min free heap: %lu bytes",
        esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
}

#endif

static void surface_task(void *arg)
{
    ESP_LOGI(TAG, "LED strip: %d LEDs, GPIO %d, type %d, color order %d",
        num_leds, CONFIG_FL_MATRIX_GPIO, sys_settings.leds.type, sys_settings.leds.color_order);
    ESP_ERROR_CHECK(strip_init());
    // the LEDs keep the last frame across a reset of the controller: a calibration step, a bright
    // effect that browned the supply out
    led_strip_clear(strip);

    // init framebuffer
    memset(&framebuffer, 0, sizeof(framebuffer_t));
    ESP_ERROR_CHECK(fb_init(&framebuffer, fb_width, fb_height, render_frame));
    prev_frame = calloc(fb_width * fb_height, sizeof(rgb_t));
    blend_frame = calloc(fb_width * fb_height, sizeof(rgb_t));
    if (!prev_frame || !blend_frame)
    {
        ESP_LOGE(TAG, "Not enough memory for frame blending");
        abort();
    }
    effect_us = esp_timer_get_time();

#if CONFIG_FL_EFFECTS_BENCHMARK
    benchmark();
#endif

    ESP_LOGI(TAG, "Surface ready");

    xEventGroupSetBits(state, BIT_READY);

    while (1)
    {
        // wait for playing bit, not clear
        xEventGroupWaitBits(state, BIT_PLAYING, pdFALSE, pdTRUE, portMAX_DELAY);

        TickType_t last_wake_time = xTaskGetTickCount();

        size_t effect = vol_settings.effect;
        uint8_t tempo = effect_tempo(effect);
        bool restart = blend_restart;
        esp_err_t res = ESP_OK;

        tempo_acc += tempo;
        if (restart || tempo_acc >= EFFECT_TEMPO_MAX)
        {
            tempo_acc = restart ? 0 : tempo_acc - EFFECT_TEMPO_MAX;
            memcpy(prev_frame, framebuffer.data, fb_width * fb_height * sizeof(rgb_t));
            effect_us += 1000000 / vol_settings.fps;
            res = effects[effect].run(&framebuffer);
            if (restart)
            {
                memcpy(prev_frame, framebuffer.data, fb_width * fb_height * sizeof(rgb_t));
                blend_restart = false;
            }
        }

        if (res == ESP_OK)
        {
            rgb_t *out = NULL;
            if (tempo < EFFECT_TEMPO_MAX)
            {
                // from the frame before the last step to the frame after it until the next step
                fract8 amount = tempo_acc * 255 / EFFECT_TEMPO_MAX;
                for (size_t i = 0; i < fb_width * fb_height; i++)
                {
                    blend_frame[i].r = blend8(prev_frame[i].r, framebuffer.data[i].r, amount);
                    blend_frame[i].g = blend8(prev_frame[i].g, framebuffer.data[i].g, amount);
                    blend_frame[i].b = blend8(prev_frame[i].b, framebuffer.data[i].b, amount);
                }
                out = blend_frame;
            }
            res = fb_render(&framebuffer, out);
            if (res != ESP_OK)
                ESP_LOGW(TAG, "Frame rendering error %d (%s)", res, esp_err_to_name(res));
        } else
            ESP_LOGW(TAG, "Effect run error %d (%s)", res, esp_err_to_name(res));

        vTaskDelayUntil(&last_wake_time, FRAME_TICKS);
    }
}

////////////////////////////////////////////////////////////////////////////////

esp_err_t surface_init()
{
    lock = xSemaphoreCreateRecursiveMutex();
    if (!lock)
        return ESP_ERR_NO_MEM;

    state = xEventGroupCreate();
    if (!state)
    {
        ESP_LOGE(TAG, "Could not create surface event group");
        return ESP_ERR_NO_MEM;
    }

    num_leds = sys_settings.leds.width * sys_settings.leds.height;

    surface_update_power();

    if (sys_settings.leds.rotation == MATRIX_ROTATION_90 || sys_settings.leds.rotation == MATRIX_ROTATION_270)
    {
        fb_width = sys_settings.leds.height;
        fb_height = sys_settings.leds.width;
    }
    else
    {
        fb_width = sys_settings.leds.width;
        fb_height = sys_settings.leds.height;
    }
    ESP_LOGI(TAG, "Surface configuration: %dx%d (%d) LEDs, rotation: %d, mirror: %d",
        fb_width, fb_height, num_leds, sys_settings.leds.rotation, sys_settings.leds.mirror);

    led_map = calloc(num_leds, sizeof(uint16_t));
    if (!led_map)
        return ESP_ERR_NO_MEM;
    build_led_map();

    xEventGroupClearBits(state, BIT_READY);

    if (xTaskCreatePinnedToCore(surface_task, "surface", SURFACE_TASK_STACK_SIZE,
            NULL, uxTaskPriorityGet(NULL) + 1, NULL, SURFACE_TASK_CORE) != pdPASS)
    {
        ESP_LOGE(TAG, "Could not create surface task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Waiting for surface task to be ready...");
    xEventGroupWaitBits(state, BIT_READY, pdFALSE, pdTRUE, portMAX_DELAY);

    if (vol_settings.effect >= effects_count)
        vol_settings.effect = 0;
    return surface_set_effect(vol_settings.effect);
}

bool surface_is_playing()
{
    return xEventGroupGetBits(state) & BIT_PLAYING;
}

static esp_err_t surface_prepare_effect_unlocked(size_t effect)
{
    if (!effects[effect].prepare)
        return ESP_OK;

    CHECK(surface_pause());
    CHECK(effects[effect].prepare(&framebuffer));
    CHECK(surface_play());

    return ESP_OK;
}

static esp_err_t surface_pause_unlocked()
{
    if (!surface_is_playing())
        return ESP_OK;

    xEventGroupClearBits(state, BIT_PLAYING);
    vTaskDelay(FRAME_TICKS + 1);

    ESP_LOGI(TAG, "Animation paused");

    return ESP_OK;
}

static esp_err_t surface_stop_unlocked()
{
    CHECK(surface_pause());

    CHECK(fb_begin(&framebuffer));
    CHECK(fb_clear(&framebuffer));
    CHECK(fb_end(&framebuffer));

    CHECK(fb_render(&framebuffer, NULL));

    ESP_LOGI(TAG, "Animation stopped");
    surface_state_changed();

    return ESP_OK;
}

static esp_err_t surface_play_unlocked()
{
    if (surface_is_playing())
        return ESP_OK;

    blend_restart = true;
    xEventGroupSetBits(state, BIT_PLAYING);
    ESP_LOGI(TAG, "Animation started");
    surface_state_changed();

    return ESP_OK;
}

static esp_err_t surface_set_effect_ex_unlocked(size_t num, bool save)
{
    CHECK_ARG(num < effects_count);

    CHECK(surface_stop());

    vol_settings.effect = num;
    if (save)
        vol_settings_save();

    if (effects[num].prepare)
        CHECK(effects[num].prepare(&framebuffer));

    CHECK(surface_play());

    ESP_LOGI(TAG, "Switched to effect %s '%s' (%d)", effects[num].code, effects[num].name, num);
    surface_state_changed();

    return ESP_OK;
}

esp_err_t surface_set_effect(size_t num)
{
    return surface_set_effect_ex(num, true);
}

esp_err_t surface_next_effect()
{
    return surface_set_effect(vol_settings.effect < effects_count - 1
            ? vol_settings.effect + 1
            : 0);
}

esp_err_t surface_prev_effect()
{
    return surface_set_effect(vol_settings.effect > 0
            ? vol_settings.effect - 1
            : effects_count - 1);
}

esp_err_t surface_set_brightness_ex(uint8_t val, bool save)
{
    if (val < BRIGHTNESS_MIN)
        val = BRIGHTNESS_MIN;
    if (val == vol_settings.brightness)
        return ESP_OK;

    // no pause needed: the renderer reads brightness on every frame
    vol_settings.brightness = val;
    if (save)
        vol_settings_save();

    ESP_LOGD(TAG, "Brightness set to %d", val);
    surface_state_changed();

    return ESP_OK;
}

esp_err_t surface_set_brightness(uint8_t val)
{
    return surface_set_brightness_ex(val, true);
}

esp_err_t surface_increment_brightness(int16_t val)
{
    int16_t b = (int16_t)(vol_settings.brightness + val);
    if (b < BRIGHTNESS_MIN) b = BRIGHTNESS_MIN;
    else if (b > 255) b = 255;
    return surface_set_brightness(b);
}

static esp_err_t surface_set_fps_unlocked(uint8_t val)
{
    CHECK_ARG(val > 0 && val <= FPS_MAX);
    if (val == vol_settings.fps)
        return ESP_OK;

    CHECK(surface_pause());

    vol_settings.fps = val;
    vol_settings_save();

    ESP_LOGI(TAG, "FPS set to %d", val);

    CHECK(surface_play());
    surface_state_changed();

    return ESP_OK;
}

void surface_set_state_cb(surface_state_cb_t cb)
{
    state_cb = cb;
}

void surface_state_changed()
{
    if (state_cb)
        state_cb();
}

esp_err_t surface_flash(rgb_t color, uint32_t ms)
{
    flash_color = color;
    if (surface_is_playing())
    {
        // the renderer shows the color instead of frames until the time is over
        flash_until = esp_timer_get_time() + ms * 1000LL;
        return ESP_OK;
    }

    // lamp is off: the surface task sleeps, drive the strip directly
    if (strip_busy)
    {
        CHECK(led_strip_refresh_async_done(strip));
        strip_busy = false;
    }
    for (size_t i = 0; i < num_leds; i++)
        CHECK(led_strip_set_pixel(strip, i, color.r, color.g, color.b));
    CHECK(led_strip_refresh(strip));
    vTaskDelay(pdMS_TO_TICKS(ms));
    return led_strip_clear(strip);
}

////////////////////////////////////////////////////////////////////////////////
/// Locked public wrappers

esp_err_t surface_prepare_effect(size_t effect)
{
    xSemaphoreTakeRecursive(lock, portMAX_DELAY);
    esp_err_t res = surface_prepare_effect_unlocked(effect);
    xSemaphoreGiveRecursive(lock);
    return res;
}

esp_err_t surface_pause()
{
    xSemaphoreTakeRecursive(lock, portMAX_DELAY);
    esp_err_t res = surface_pause_unlocked();
    xSemaphoreGiveRecursive(lock);
    return res;
}

esp_err_t surface_stop()
{
    xSemaphoreTakeRecursive(lock, portMAX_DELAY);
    esp_err_t res = surface_stop_unlocked();
    xSemaphoreGiveRecursive(lock);
    return res;
}

esp_err_t surface_play()
{
    xSemaphoreTakeRecursive(lock, portMAX_DELAY);
    esp_err_t res = surface_play_unlocked();
    xSemaphoreGiveRecursive(lock);
    return res;
}

void surface_update_power()
{
    white_brightness = power_white_brightness();
    ESP_LOGI(TAG, "Current limit %lu mA: full white brightness %lu, kept after brownouts %d %%",
            sys_settings.leds.current_limit, white_brightness / 256, sys_settings.power.percent);
}

esp_err_t surface_show_white(int16_t brightness)
{
    calib_white = brightness;
    // the frames are rendered only while the animation plays
    if (brightness >= 0 && !surface_is_playing())
        return surface_play();
    return ESP_OK;
}

esp_err_t surface_hold_for_update(bool hold)
{
    static bool held = false, was_playing = false;

    xSemaphoreTakeRecursive(lock, portMAX_DELAY);
    esp_err_t res = ESP_OK;
    if (hold && !held)
    {
        held = true;
        was_playing = surface_is_playing();
        res = surface_stop_unlocked();
    }
    else if (!hold && held)
    {
        held = false;
        if (was_playing)
            res = surface_play_unlocked();
    }
    xSemaphoreGiveRecursive(lock);
    return res;
}

esp_err_t surface_set_effect_ex(size_t num, bool save)
{
    xSemaphoreTakeRecursive(lock, portMAX_DELAY);
    esp_err_t res = surface_set_effect_ex_unlocked(num, save);
    xSemaphoreGiveRecursive(lock);
    return res;
}

esp_err_t surface_set_fps(uint8_t val)
{
    xSemaphoreTakeRecursive(lock, portMAX_DELAY);
    esp_err_t res = surface_set_fps_unlocked(val);
    xSemaphoreGiveRecursive(lock);
    return res;
}
