#include "effect.h"
#include <string.h>
#include <esp_crc.h>
#include "storage.h"
#include "settings.h"
#include "surface.h"
#include "lib8tion.h"

////////////////////////////////////////////////////////////////////////////////

#include "effects/crazybees.h"
#include "effects/dna.h"
#include "effects/fire.h"
#include "effects/lamp.h"
#include "effects/lava.h"
#include "effects/matrix.h"
#include "effects/plasma_waves.h"
#include "effects/rain.h"
#include "effects/rainbow.h"
#include "effects/rays.h"
#include "effects/sparkles.h"
#include "effects/waterfall.h"
#include "effects/metaballs.h"
#include "effects/water.h"
#include "effects/holiday.h"
#include "effects/hoop.h"
#include "effects/twister.h"
#include "effects/flower.h"
#include "effects/polar_lights.h"
#include "effects/gif.h"
#include "effects/relax.h"
#include "effects/wheel.h"
#include "effects/peacock.h"
#include "effects/diagonals.h"
#include "effects/whirls.h"
#include "effects/sun.h"
#include "effects/spaceships.h"
#include "effects/fireflies.h"
#include "effects/tunnel.h"
#include "effects/dunes.h"
#include "effects/octopus.h"
#include "effects/nuclear.h"

const effect_descriptor_t effects[] = {
    DESCR_EFFECT_LAMP,
    DESCR_EFFECT_FIRE,
    DESCR_EFFECT_SPARKLES,
    DESCR_EFFECT_LAVA,
    DESCR_EFFECT_WATERFALL,
    DESCR_EFFECT_PLASMA_WAVES,
    DESCR_EFFECT_DNA,
    DESCR_EFFECT_MATRIX,
    DESCR_EFFECT_CRAZYBEES,
    DESCR_EFFECT_RAIN,
    DESCR_EFFECT_RAINBOW,
    DESCR_EFFECT_RAYS,
    DESCR_EFFECT_METABALLS,
    DESCR_EFFECT_WATER,
    DESCR_EFFECT_HOLIDAY,
    DESCR_EFFECT_HOOP,
    DESCR_EFFECT_TWISTER,
    DESCR_EFFECT_FLOWER,
    DESCR_EFFECT_POLAR_LIGHTS,
    DESCR_EFFECT_GIF,
    DESCR_EFFECT_RELAX,
    DESCR_EFFECT_WHEEL,
    DESCR_EFFECT_PEACOCK,
    DESCR_EFFECT_DIAGONALS,
    DESCR_EFFECT_WHIRLS,
    DESCR_EFFECT_SUN,
    DESCR_EFFECT_SPACESHIPS,
    DESCR_EFFECT_FIREFLIES,
    DESCR_EFFECT_TUNNEL,
    DESCR_EFFECT_DUNES,
    DESCR_EFFECT_OCTOPUS,
    DESCR_EFFECT_NUCLEAR,
};

const size_t effects_count = sizeof(effects) / sizeof(effect_descriptor_t);

#define PARAM(effect, param) effects[(effect)].params[(param)]

////////////////////////////////////////////////////////////////////////////////

// Record format: sequence of entries
//   uint16_t crc16 of effect name, uint8_t number of params, uint8_t values[number of params]
// Bound to effect names, so the values survive adding or reordering effects in firmware updates.

static uint16_t name_hash(const char *name)
{
    return esp_crc16_le(0, (const uint8_t *)name, strlen(name));
}

static esp_err_t effects_save_now()
{
    uint8_t buf[STORAGE_EFFECTS_MAX_SIZE];
    size_t len = 0;

    for (size_t e = 0; e < effects_count; e++)
    {
        size_t count = effects[e].params_count;
        if (len + 3 + count > sizeof(buf))
        {
            ESP_LOGE(TAG, "Effect parameters do not fit storage record");
            return ESP_ERR_NO_MEM;
        }
        uint16_t hash = name_hash(effects[e].name);
        buf[len++] = hash & 0xff;
        buf[len++] = hash >> 8;
        buf[len++] = count;
        for (size_t p = 0; p < count; p++)
            buf[len++] = PARAM(e, p).value;
    }

    return storage_save(STORAGE_EFFECTS, buf, len);
}

esp_err_t effects_reset()
{
    for (size_t e = 0; e < effects_count; e++)
        for (size_t p = 0; p < effects[e].params_count; p++)
            PARAM(e, p).value = PARAM(e, p).def;

    CHECK(effects_save_now());

    ESP_LOGW(TAG, "Effect parameters have been reset to defaults");

    return ESP_OK;
}

esp_err_t effect_param_set(size_t effect, size_t param, uint8_t value)
{
    CHECK_ARG(effect < effects_count
            && param < effects[effect].params_count
            && value >= PARAM(effect, param).min
            && value <= PARAM(effect, param).max);

    if (PARAM(effect, param).value == value)
        return ESP_OK;

    ESP_LOGI(TAG, "'%s':'%s' = %d", effects[effect].name, PARAM(effect, param).name, value);

    PARAM(effect, param).value = value;
    storage_defer(STORAGE_EFFECTS, effects_save_now);

    if (effect == vol_settings.effect)
        CHECK_LOGE(surface_prepare_effect(effect), "Could not prepare effect");

    return ESP_OK;
}

static esp_err_t effect_params_load()
{
    uint8_t buf[STORAGE_EFFECTS_MAX_SIZE];
    size_t len = sizeof(buf);
    CHECK(storage_load(STORAGE_EFFECTS, buf, &len));

    size_t pos = 0;
    while (pos + 3 <= len)
    {
        uint16_t hash = buf[pos] | (buf[pos + 1] << 8);
        size_t count = buf[pos + 2];
        pos += 3;
        if (pos + count > len)
            break;

        for (size_t e = 0; e < effects_count; e++)
        {
            if (name_hash(effects[e].name) != hash)
                continue;
            for (size_t p = 0; p < effects[e].params_count && p < count; p++)
            {
                uint8_t val = buf[pos + p];
                if (val >= PARAM(e, p).min && val <= PARAM(e, p).max)
                    PARAM(e, p).value = val;
            }
            break;
        }
        pos += count;
    }

    for (size_t e = 0; e < effects_count; e++)
    {
        ESP_LOGI(TAG, "Effect %d: '%s'", e, effects[e].name);
        for (size_t p = 0; p < effects[e].params_count; p++)
            ESP_LOGI(TAG, "    %s: %d (min %d, max %d, def %d)",
                    PARAM(e, p).name, PARAM(e, p).value, PARAM(e, p).min, PARAM(e, p).max, PARAM(e, p).def);
    }

    ESP_LOGI(TAG, "Effect parameters loaded");

    return ESP_OK;
}

esp_err_t effects_init()
{
    esp_err_t res = effect_params_load();
    if (res != ESP_OK)
        res = effects_reset();

    random16_set_seed(esp_cpu_get_cycle_count() & 0xffff);

    return res;
}
