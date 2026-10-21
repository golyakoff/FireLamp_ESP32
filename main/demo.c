#include "demo.h"
#include <string.h>
#include <esp_timer.h>
#include <esp_random.h>
#include <esp_crc.h>
#include "effect.h"
#include "settings.h"
#include "surface.h"
#include "storage.h"
#include "bus.h"

#define MAX_EXCLUDED 48

// Storage format: excluded effects are kept as hashes of their codes,
// so effects added by firmware updates are included by default
typedef struct __attribute__((packed))
{
    uint8_t order;
    uint16_t duration;
    uint16_t random;
    uint8_t excluded_count;
    uint16_t excluded[MAX_EXCLUDED];
} stored_demo_t;

_Static_assert(sizeof(stored_demo_t) <= STORAGE_DEMO_MAX_SIZE, "Demo settings do not fit storage record");

static demo_settings_t settings = {
    .order = DEMO_ORDER_SEQUENTIAL,
    .duration = 60,
    .random = 0,
};
static bool included[MAX_EFFECTS];
static esp_timer_handle_t timer = NULL;

static uint16_t code_hash(const char *code)
{
    return esp_crc16_le(0, (const uint8_t *)code, strlen(code));
}

static bool settings_valid(const demo_settings_t *s)
{
    return s->order < DEMO_ORDER_MAX
        && s->duration >= DEMO_MIN_DURATION && s->duration <= DEMO_MAX_DURATION
        && s->random <= DEMO_MAX_RANDOM;
}

static esp_err_t save()
{
    stored_demo_t d = {
        .order = settings.order,
        .duration = settings.duration,
        .random = settings.random,
    };
    for (size_t e = 0; e < effects_count && d.excluded_count < MAX_EXCLUDED; e++)
        if (!included[e])
            d.excluded[d.excluded_count++] = code_hash(effects[e].code);

    return storage_save(STORAGE_DEMO, &d, sizeof(d) - (MAX_EXCLUDED - d.excluded_count) * sizeof(uint16_t));
}

static void timer_cb(void *arg)
{
    bus_send_event(EVENT_DEMO_NEXT, NULL, 0);
}

static void restart_timer()
{
    esp_timer_stop(timer);
    uint64_t sec = settings.duration;
    if (settings.random)
        sec += esp_random() % (settings.random + 1);
    esp_timer_start_once(timer, sec * 1000000ULL);
    ESP_LOGD(TAG, "Demo: next effect in %llu s", sec);
}

static esp_err_t switch_to(size_t effect)
{
    if (effect != vol_settings.effect)
        CHECK(surface_set_effect_ex(effect, false));
    restart_timer();
    return ESP_OK;
}

////////////////////////////////////////////////////////////////////////////////

esp_err_t demo_reset()
{
    settings.order = DEMO_ORDER_SEQUENTIAL;
    settings.duration = 60;
    settings.random = 0;
    for (size_t e = 0; e < effects_count; e++)
        included[e] = true;
    return save();
}

esp_err_t demo_init()
{
    if (effects_count > MAX_EFFECTS)
    {
        ESP_LOGE(TAG, "Too many effects for the demo mode");
        return ESP_ERR_NO_MEM;
    }

    esp_timer_create_args_t args = {
        .callback = timer_cb,
        .name = "demo",
    };
    CHECK(esp_timer_create(&args, &timer));

    for (size_t e = 0; e < effects_count; e++)
        included[e] = true;

    stored_demo_t d;
    size_t size = sizeof(d);
    bool loaded = storage_load(STORAGE_DEMO, &d, &size) == ESP_OK
            && size >= offsetof(stored_demo_t, excluded)
            && d.excluded_count <= MAX_EXCLUDED
            && size == offsetof(stored_demo_t, excluded) + d.excluded_count * sizeof(uint16_t);
    demo_settings_t s = { .order = d.order, .duration = d.duration, .random = d.random };
    if (loaded && settings_valid(&s))
    {
        settings = s;
        for (size_t e = 0; e < effects_count; e++)
            for (size_t i = 0; i < d.excluded_count; i++)
                if (code_hash(effects[e].code) == d.excluded[i])
                    included[e] = false;
    }
    else
    {
        ESP_LOGW(TAG, "No valid demo settings stored, using defaults");
        CHECK(demo_reset());
    }

    if (vol_settings.demo)
        restart_timer();

    ESP_LOGI(TAG, "Demo mode %s, order %d, duration %d s + random %d s",
            vol_settings.demo ? "on" : "off", settings.order, settings.duration, settings.random);

    return ESP_OK;
}

void demo_get_settings(demo_settings_t *s)
{
    *s = settings;
}

esp_err_t demo_set_settings(const demo_settings_t *s)
{
    CHECK_ARG(s && settings_valid(s));
    settings = *s;
    if (vol_settings.demo)
        restart_timer();
    return save();
}

bool demo_is_included(size_t effect)
{
    return effect < effects_count && included[effect];
}

esp_err_t demo_set_included(size_t effect, bool inc)
{
    CHECK_ARG(effect < effects_count);
    if (included[effect] == inc)
        return ESP_OK;

    if (!inc)
    {
        size_t n = 0;
        for (size_t e = 0; e < effects_count; e++)
            n += included[e];
        if (n <= 1)
            return ESP_ERR_INVALID_STATE;
    }
    included[effect] = inc;
    return save();
}

bool demo_is_active()
{
    return vol_settings.demo;
}

esp_err_t demo_start()
{
    if (!vol_settings.demo)
    {
        vol_settings.demo = true;
        vol_settings_save();
        ESP_LOGI(TAG, "Demo mode started");
        surface_state_changed();
    }
    restart_timer();
    return ESP_OK;
}

void demo_stop()
{
    if (!vol_settings.demo)
        return;
    esp_timer_stop(timer);
    vol_settings.demo = false;
    // remember the effect that was playing
    vol_settings_save();
    ESP_LOGI(TAG, "Demo mode stopped");
    surface_state_changed();
}

esp_err_t demo_skip(int dir)
{
    CHECK_ARG(vol_settings.demo);
    return switch_to(demo_pick_next(vol_settings.effect, included, effects_count, DEMO_ORDER_SEQUENTIAL, dir, 0));
}

esp_err_t demo_next_effect(int dir)
{
    if (vol_settings.demo)
        return demo_skip(dir);
    return dir > 0 ? surface_next_effect() : surface_prev_effect();
}

esp_err_t demo_on_timer()
{
    if (!vol_settings.demo)
        return ESP_OK;
    if (!surface_is_playing())
    {
        // lamp is off: keep the effect, check again later
        restart_timer();
        return ESP_OK;
    }
    return switch_to(demo_pick_next(vol_settings.effect, included, effects_count, settings.order, 1, esp_random()));
}
