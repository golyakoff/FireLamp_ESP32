#include "settings.h"
#include <string.h>
#include "storage.h"
#include "surface.h"
#include "effect.h"

_Static_assert(sizeof(system_settings_t) <= STORAGE_SYSTEM_MAX_SIZE, "System settings do not fit storage record");

system_settings_t sys_settings = { 0 };
volatile_settings_t vol_settings = { 0 };

static system_settings_t sys_defaults = {
    .wifi = {
        .mode = DEFAULT_WIFI_MODE,
        .ip = {
            .dhcp    = DEFAULT_WIFI_DHCP,
            .ip      = CONFIG_FL_WIFI_IP,
            .netmask = CONFIG_FL_WIFI_NETMASK,
            .gateway = CONFIG_FL_WIFI_GATEWAY,
            .dns     = CONFIG_FL_WIFI_DNS,
        },
        .ap = {
            .ssid           = CONFIG_FL_WIFI_AP_SSID,
            .channel        = CONFIG_FL_WIFI_AP_CHANNEL,
            .password       = CONFIG_FL_WIFI_AP_PASSWD,
            .max_connection = DEFAULT_WIFI_AP_MAXCONN,
            .authmode       = WIFI_AUTH_WPA_WPA2_PSK
        },
        .sta = {
            .ssid               = CONFIG_FL_WIFI_STA_SSID,
            .password           = CONFIG_FL_WIFI_STA_PASSWD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    },
    .time = {
        .tz = CONFIG_FL_TZ_DEFAULT,
    },
    .wifi_client = {
        .mode = WIFI_CLIENT_OFF,
        .period = 1,
    },
    .leds = {
        .width         = CONFIG_FL_MATRIX_WIDTH,
        .height        = CONFIG_FL_MATRIX_HEIGHT,
        .type          = DEFAULT_LED_TYPE,
        .color_order   = DEFAULT_LED_COLOR_ORDER,
        .rotation      = DEFAULT_MATRIX_ROTATION,
        .mirror        = DEFAULT_MATRIX_MIRROR,
        .current_limit = CONFIG_FL_MATRIX_MAX_CURRENT,
    },
};

// Storage format of volatile settings
typedef struct __attribute__((packed))
{
    uint8_t fps;
    uint8_t brightness;
    uint8_t demo;
    char effect[EFFECT_CODE_MAX_LEN + 1];
} stored_volatile_t;

_Static_assert(sizeof(stored_volatile_t) <= STORAGE_VOLATILE_MAX_SIZE, "Volatile settings do not fit storage record");

static size_t default_effect()
{
    int e = effect_find(CONFIG_FL_EFFECT_DEFAULT);
    if (e < 0)
    {
        ESP_LOGE(TAG, "Default effect '%s' not found", CONFIG_FL_EFFECT_DEFAULT);
        e = 0;
    }
    return e;
}

static esp_err_t load(storage_record_t rec, void *target, size_t size)
{
    size_t len = size;
    CHECK(storage_load(rec, target, &len));
    if (len != size)
    {
        ESP_LOGW(TAG, "Invalid settings size %d, expected %d", len, size);
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t vol_settings_save_now()
{
    stored_volatile_t v = {
        .fps = vol_settings.fps,
        .brightness = vol_settings.brightness,
        .demo = vol_settings.demo,
    };
    strncpy(v.effect, effects[vol_settings.effect < effects_count ? vol_settings.effect : 0].code, EFFECT_CODE_MAX_LEN);
    return storage_save(STORAGE_VOLATILE, &v, sizeof(v));
}

////////////////////////////////////////////////////////////////////////////////

esp_err_t settings_init()
{
    return storage_init();
}

esp_err_t sys_settings_reset()
{
    if (!strlen((const char *) sys_defaults.wifi.ap.password))
        sys_defaults.wifi.ap.authmode = WIFI_AUTH_OPEN;
    if (!strlen((const char *) sys_defaults.wifi.sta.password))
        sys_defaults.wifi.sta.threshold.authmode = WIFI_AUTH_OPEN;

    memcpy(&sys_settings, &sys_defaults, sizeof(system_settings_t));
    CHECK(sys_settings_save());

    ESP_LOGW(TAG, "System settings have been reset to defaults");

    return ESP_OK;
}

// Records of older firmware versions end before these fields
#define SYS_SETTINGS_MIN_SIZE offsetof(system_settings_t, ble)

esp_err_t sys_settings_load()
{
    uint8_t buf[STORAGE_SYSTEM_MAX_SIZE];
    size_t size = sizeof(buf);
    if (storage_load(STORAGE_SYSTEM, buf, &size) != ESP_OK
            || size < SYS_SETTINGS_MIN_SIZE || size > sizeof(system_settings_t))
    {
        ESP_LOGW(TAG, "No valid system settings stored");
        return sys_settings_reset();
    }

    // defaults for the fields missing in an older record
    memcpy(&sys_settings, &sys_defaults, sizeof(system_settings_t));
    memcpy(&sys_settings, buf, size);
    if (size < sizeof(system_settings_t))
    {
        ESP_LOGW(TAG, "System settings upgraded from %d to %d bytes", size, sizeof(system_settings_t));
        CHECK(sys_settings_save());
    }

    return ESP_OK;
}

esp_err_t sys_settings_save()
{
    return storage_save(STORAGE_SYSTEM, &sys_settings, sizeof(system_settings_t));
}

esp_err_t vol_settings_reset()
{
    vol_settings.fps = CONFIG_FL_FPS_DEFAULT;
    vol_settings.brightness = CONFIG_FL_BRIGHTNESS_DEFAULT;
    vol_settings.effect = default_effect();
    vol_settings.demo = false;
    CHECK(vol_settings_save_now());

    ESP_LOGW(TAG, "Volatile settings have been reset to defaults");

    return ESP_OK;
}

esp_err_t vol_settings_load()
{
    stored_volatile_t v;
    if (load(STORAGE_VOLATILE, &v, sizeof(v)) != ESP_OK
            || !v.fps || v.fps > FPS_MAX || v.brightness < BRIGHTNESS_MIN)
        return vol_settings_reset();

    v.effect[EFFECT_CODE_MAX_LEN] = 0;
    int e = effect_find(v.effect);
    if (e < 0)
        ESP_LOGW(TAG, "Stored effect '%s' not found, using default", v.effect);

    vol_settings.fps = v.fps;
    vol_settings.brightness = v.brightness;
    vol_settings.demo = v.demo;
    vol_settings.effect = e < 0 ? default_effect() : (size_t)e;

    return ESP_OK;
}

esp_err_t vol_settings_save()
{
    storage_defer(STORAGE_VOLATILE, vol_settings_save_now);
    return ESP_OK;
}
