#include "effect.h"
#include <string.h>
#include "code_hash.h"
#include "storage.h"
#include "settings.h"
#include "surface.h"
#include "lib8tion.h"

////////////////////////////////////////////////////////////////////////////////

#include "effects/crazybees.h"
#include "effects/dna.h"
#include "effects/fire.h"
#include "effects/gyver_fire.h"
#include "effects/petrick_fire.h"
#include "effects/lamp.h"
#include "effects/lava.h"
#include "effects/matrix.h"
#include "effects/plasma_waves.h"
#include "effects/rain.h"
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
#include "effects/starfall.h"
#include "effects/storm.h"
#include "effects/twinkles.h"
#include "effects/butterflies.h"
#include "effects/streams.h"
#include "effects/trail_fireflies.h"
#include "effects/glass_drops.h"
#include "effects/magma.h"
#include "effects/snow.h"
#include "effects/prismata.h"
#include "effects/clouds.h"

const effect_descriptor_t effects[] = {
    // similar looking effects next to each other: the app and the sequential demo mode go in this order
    DESCR_EFFECT_FIRE,
    DESCR_EFFECT_GYVER_FIRE,
    DESCR_EFFECT_PETRICK_FIRE,
    DESCR_EFFECT_WHIRLS,
    DESCR_EFFECT_SOARING_FIRE,
    DESCR_EFFECT_TOP_FIRE,
    DESCR_EFFECT_MAGMA,
    DESCR_EFFECT_LAVA,
    DESCR_EFFECT_METABALLS,
    DESCR_EFFECT_SUN,
    DESCR_EFFECT_PEACOCK,
    DESCR_EFFECT_PLASMA_WAVES,
    DESCR_EFFECT_HOOP,
    DESCR_EFFECT_TWISTER,
    DESCR_EFFECT_DNA,
    DESCR_EFFECT_PRISMATA,
    DESCR_EFFECT_OCTOPUS,
    DESCR_EFFECT_RAINBOW_SNAKE,
    DESCR_EFFECT_NUCLEAR,
    DESCR_EFFECT_DUNES,
    DESCR_EFFECT_CLOUDS,
    DESCR_EFFECT_POLAR_LIGHTS,
    DESCR_EFFECT_RAYS,
    DESCR_EFFECT_WHEEL,
    DESCR_EFFECT_FLOWER,
    DESCR_EFFECT_TUNNEL,
    DESCR_EFFECT_GIF,
    DESCR_EFFECT_WATER,
    DESCR_EFFECT_WATERFALL,
    DESCR_EFFECT_GLASS_DROPS,
    DESCR_EFFECT_MATRIX,
    DESCR_EFFECT_RAIN,
    DESCR_EFFECT_STORM,
    DESCR_EFFECT_SNOW,
    DESCR_EFFECT_STARFALL,
    DESCR_EFFECT_SPARKLES,
    DESCR_EFFECT_TWINKLES,
    DESCR_EFFECT_FIREFLIES,
    DESCR_EFFECT_TRAIL_FIREFLIES,
    DESCR_EFFECT_CRAZYBEES,
    DESCR_EFFECT_BUTTERFLIES,
    DESCR_EFFECT_SPACESHIPS,
    DESCR_EFFECT_DIAGONALS,
    DESCR_EFFECT_HOLIDAY,
    DESCR_EFFECT_LAMP,
};

const size_t effects_count = sizeof(effects) / sizeof(effect_descriptor_t);

#define PARAM(effect, param) effects[(effect)].params[(param)]

////////////////////////////////////////////////////////////////////////////////

// Record format: sequence of entries
//   uint16_t code_hash() of effect code, uint8_t number of params N,
//   N x (uint16_t code_hash() of param code, uint8_t value)
// Bound to codes, so the values survive adding, removing or reordering effects and parameters.

static effect_param_cb_t param_cb = NULL;

// index of the `tempo` parameter of every effect, -1 if it has none (effects_init())
static int8_t tempo_param[MAX_EFFECTS] = { [0 ... MAX_EFFECTS - 1] = -1 };

// brightest frame of every effect, percent of the full white matrix (effects_init())
static const struct {
    const char *code;
    uint8_t peak;
} peak_table[] = {
#define EFFECT_PEAK(CODE, PERCENT) { #CODE, PERCENT },
#include "effect_peaks.h"
#undef EFFECT_PEAK
};
static uint8_t peaks[MAX_EFFECTS] = { [0 ... MAX_EFFECTS - 1] = 100 };

void effect_set_param_cb(effect_param_cb_t cb)
{
    param_cb = cb;
}

static esp_err_t effects_save_now()
{
    uint8_t buf[STORAGE_EFFECTS_MAX_SIZE];
    size_t len = 0;

    for (size_t e = 0; e < effects_count; e++)
    {
        size_t count = effects[e].params_count;
        if (len + 3 + count * 3 > sizeof(buf))
        {
            ESP_LOGE(TAG, "Effect parameters do not fit storage record");
            return ESP_ERR_NO_MEM;
        }
        uint16_t hash = code_hash(effects[e].code);
        buf[len++] = hash & 0xff;
        buf[len++] = hash >> 8;
        buf[len++] = count;
        for (size_t p = 0; p < count; p++)
        {
            hash = code_hash(PARAM(e, p).code);
            buf[len++] = hash & 0xff;
            buf[len++] = hash >> 8;
            buf[len++] = PARAM(e, p).value;
        }
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

    ESP_LOGI(TAG, "%s.%s = %d", effects[effect].code, PARAM(effect, param).code, value);

    PARAM(effect, param).value = value;
    storage_defer(STORAGE_EFFECTS, effects_save_now);
    if (param_cb)
        param_cb(effect, param);

    // the tempo is read by the surface every frame, restarting the effect is not needed
    if (effect == vol_settings.effect && (int)param != tempo_param[effect])
        CHECK_LOGE(surface_prepare_effect(effect), "Could not prepare effect");

    return ESP_OK;
}

uint8_t effect_tempo(size_t effect)
{
    if (effect >= effects_count || tempo_param[effect] < 0)
        return EFFECT_TEMPO_MAX;
    return PARAM(effect, tempo_param[effect]).value;
}

uint8_t effect_peak(size_t effect)
{
    return effect < effects_count && effect < MAX_EFFECTS ? peaks[effect] : 100;
}

static void apply_stored_params(size_t e, const uint8_t *entry, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        uint16_t hash = entry[i * 3] | (entry[i * 3 + 1] << 8);
        uint8_t val = entry[i * 3 + 2];
        for (size_t p = 0; p < effects[e].params_count; p++)
        {
            if (code_hash(PARAM(e, p).code) != hash)
                continue;
            if (val >= PARAM(e, p).min && val <= PARAM(e, p).max)
                PARAM(e, p).value = val;
            break;
        }
    }
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
        if (pos + count * 3 > len)
            break;

        for (size_t e = 0; e < effects_count; e++)
            if (code_hash(effects[e].code) == hash)
            {
                apply_stored_params(e, buf + pos, count);
                break;
            }
        pos += count * 3;
    }

    for (size_t e = 0; e < effects_count; e++)
    {
        ESP_LOGI(TAG, "Effect %d: %s '%s'", e, effects[e].code, effects[e].name);
        for (size_t p = 0; p < effects[e].params_count; p++)
            ESP_LOGI(TAG, "    %s: %d (min %d, max %d, def %d)",
                    PARAM(e, p).code, PARAM(e, p).value, PARAM(e, p).min, PARAM(e, p).max, PARAM(e, p).def);
    }

    ESP_LOGI(TAG, "Effect parameters loaded");

    return ESP_OK;
}

bool effect_code_valid(const char *code)
{
    size_t len = code ? strlen(code) : 0;
    if (!len || len > EFFECT_CODE_MAX_LEN)
        return false;
    for (size_t i = 0; i < len; i++)
        if (!((code[i] >= 'a' && code[i] <= 'z') || (code[i] >= '0' && code[i] <= '9') || code[i] == '_'))
            return false;
    return true;
}

int effect_find(const char *code)
{
    if (!code)
        return -1;
    for (size_t e = 0; e < effects_count; e++)
        if (!strcmp(effects[e].code, code))
            return e;
    return -1;
}

int effect_param_find(size_t effect, const char *code)
{
    if (effect >= effects_count || !code)
        return -1;
    for (size_t p = 0; p < effects[effect].params_count; p++)
        if (!strcmp(PARAM(effect, p).code, code))
            return p;
    return -1;
}

// Codes are stored as hashes: they must be valid and the hashes unique
static void check_codes()
{
    for (size_t e = 0; e < effects_count; e++)
    {
        if (!effect_code_valid(effects[e].code))
            ESP_LOGE(TAG, "Invalid effect code '%s'", effects[e].code);
        for (size_t e2 = e + 1; e2 < effects_count; e2++)
            if (code_hash(effects[e].code) == code_hash(effects[e2].code))
                ESP_LOGE(TAG, "Effect codes '%s' and '%s' collide", effects[e].code, effects[e2].code);
        for (size_t p = 0; p < effects[e].params_count; p++)
        {
            if (!effect_code_valid(PARAM(e, p).code))
                ESP_LOGE(TAG, "Invalid parameter code '%s.%s'", effects[e].code, PARAM(e, p).code);
            for (size_t p2 = p + 1; p2 < effects[e].params_count; p2++)
                if (code_hash(PARAM(e, p).code) == code_hash(PARAM(e, p2).code))
                    ESP_LOGE(TAG, "Parameter codes '%s.%s' and '%s' collide",
                            effects[e].code, PARAM(e, p).code, PARAM(e, p2).code);
        }
    }
}

esp_err_t effects_init()
{
    check_codes();

    for (size_t e = 0; e < effects_count && e < MAX_EFFECTS; e++)
    {
        tempo_param[e] = effect_param_find(e, "tempo");

        size_t i = 0;
        while (i < sizeof(peak_table) / sizeof(peak_table[0]) && strcmp(peak_table[i].code, effects[e].code))
            i++;
        if (i < sizeof(peak_table) / sizeof(peak_table[0]))
            peaks[e] = peak_table[i].peak;
        else
            ESP_LOGW(TAG, "No peak of effect '%s' (run tools/preview/effect_peaks.py), capped as full white", effects[e].code);
    }

    esp_err_t res = effect_params_load();
    if (res != ESP_OK)
        res = effects_reset();

    random16_set_seed(esp_cpu_get_cycle_count() & 0xffff);

    return res;
}
