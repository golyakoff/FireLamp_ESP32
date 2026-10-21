#include "settings.h"
#include <string.h>
#include "storage.h"
#include "surface.h"
#include "effect.h"

_Static_assert(sizeof(system_settings_t) <= STORAGE_SYSTEM_MAX_SIZE, "System settings do not fit storage record");

system_settings_t sys_settings = { 0 };
volatile_settings_t vol_settings = { 0 };

static const system_settings_t sys_defaults = {
    .version = SYS_SETTINGS_VERSION,
    .time = {
        .tz = CONFIG_FL_TZ_DEFAULT,
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
    .wifi = {
        .mode     = DEFAULT_WIFI_MODE,
        .period   = CONFIG_FL_WIFI_PERIOD,
        .ssid     = CONFIG_FL_WIFI_SSID,
        .password = CONFIG_FL_WIFI_PASSWORD,
        .dhcp     = DEFAULT_WIFI_DHCP,
        .ip       = CONFIG_FL_WIFI_IP,
        .netmask  = CONFIG_FL_WIFI_NETMASK,
        .gateway  = CONFIG_FL_WIFI_GATEWAY,
        .dns      = CONFIG_FL_WIFI_DNS,
    },
};

// The size of the first version 2 record. Records of later firmware versions may be longer.
#define SYS_SETTINGS_MIN_SIZE sizeof(system_settings_t)

////////////////////////////////////////////////////////////////////////////////
/// Migration from version 1 (firmware stages 4-5): EvLamp WiFi AP/STA settings
/// at the beginning, WiFi client settings at the end

typedef struct
{
    struct {
        uint32_t mode;  // WIFI_MODE_STA = 1, WIFI_MODE_AP = 2 (esp_wifi)
        struct {
            bool dhcp;
            char ip[16];
            char netmask[16];
            char gateway[16];
            char dns[16];
        } ip;
        wifi_ap_config_t ap;
        wifi_sta_config_t sta;
    } wifi;
    struct {
        char tz[48];
    } time;
    struct {
        uint8_t width;
        uint8_t height;
        uint8_t type;
        uint8_t color_order;
        uint8_t rotation;
        bool mirror;
        uint32_t current_limit;
    } leds;
    // stage 5 only
    struct {
        char name[BLE_NAME_MAX_LEN + 1];
    } ble;
    struct {
        uint8_t mode;
        uint8_t period;
        char ssid[33];
        char password[65];
    } wifi_client;
} system_settings_v1_t;

static bool migrate_v1(const uint8_t *buf, size_t size)
{
    const system_settings_v1_t *v1 = (const system_settings_v1_t *)buf;
    if (size < offsetof(system_settings_v1_t, ble) || size > sizeof(system_settings_v1_t)
            || (v1->wifi.mode != 1 && v1->wifi.mode != 2))
        return false;

    memcpy(&sys_settings, &sys_defaults, sizeof(system_settings_t));
    memcpy(sys_settings.time.tz, v1->time.tz, sizeof(sys_settings.time.tz));
    sys_settings.time.tz[sizeof(sys_settings.time.tz) - 1] = 0;
    memcpy(&sys_settings.leds, &v1->leds, sizeof(sys_settings.leds));

    sys_settings.wifi.dhcp = v1->wifi.ip.dhcp;
    memcpy(sys_settings.wifi.ip, v1->wifi.ip.ip, 16);
    memcpy(sys_settings.wifi.netmask, v1->wifi.ip.netmask, 16);
    memcpy(sys_settings.wifi.gateway, v1->wifi.ip.gateway, 16);
    memcpy(sys_settings.wifi.dns, v1->wifi.ip.dns, 16);

    if (size == sizeof(system_settings_v1_t))
    {
        // stage 5: WiFi client settings made over BLE
        memcpy(sys_settings.ble.name, v1->ble.name, sizeof(sys_settings.ble.name));
        sys_settings.wifi.mode = v1->wifi_client.mode < WIFI_CLIENT_MODE_MAX ? v1->wifi_client.mode : WIFI_CLIENT_OFF;
        sys_settings.wifi.period = v1->wifi_client.period;
        memcpy(sys_settings.wifi.ssid, v1->wifi_client.ssid, sizeof(sys_settings.wifi.ssid));
        memcpy(sys_settings.wifi.password, v1->wifi_client.password, sizeof(sys_settings.wifi.password));
    }
    else if (v1->wifi.mode == 1 && v1->wifi.sta.ssid[0])
    {
        // stage 4: EvLamp station mode, it was always connected
        sys_settings.wifi.mode = WIFI_CLIENT_ALWAYS_ON;
        memcpy(sys_settings.wifi.ssid, v1->wifi.sta.ssid, WIFI_SSID_MAX_LEN);
        memcpy(sys_settings.wifi.password, v1->wifi.sta.password, WIFI_PASSWORD_MAX_LEN);
    }
    sys_settings.ble.name[BLE_NAME_MAX_LEN] = 0;
    sys_settings.wifi.ssid[WIFI_SSID_MAX_LEN] = 0;
    sys_settings.wifi.password[WIFI_PASSWORD_MAX_LEN] = 0;
    sys_settings.wifi.ip[15] = 0;
    sys_settings.wifi.netmask[15] = 0;
    sys_settings.wifi.gateway[15] = 0;
    sys_settings.wifi.dns[15] = 0;
    if (sys_settings.wifi.period < 1 || sys_settings.wifi.period > WIFI_PERIOD_MAX)
        sys_settings.wifi.period = CONFIG_FL_WIFI_PERIOD;

    ESP_LOGW(TAG, "System settings migrated from version 1 (%d bytes)", size);
    return true;
}

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
    memcpy(&sys_settings, &sys_defaults, sizeof(system_settings_t));
    CHECK(sys_settings_save());

    ESP_LOGW(TAG, "System settings have been reset to defaults");

    return ESP_OK;
}

esp_err_t sys_settings_load()
{
    uint8_t buf[STORAGE_SYSTEM_MAX_SIZE];
    size_t size = sizeof(buf);
    if (storage_load(STORAGE_SYSTEM, buf, &size) != ESP_OK || size < sizeof(uint32_t))
    {
        ESP_LOGW(TAG, "No valid system settings stored");
        return sys_settings_reset();
    }

    uint32_t version;
    memcpy(&version, buf, sizeof(version));
    if (version == SYS_SETTINGS_VERSION && size >= SYS_SETTINGS_MIN_SIZE && size <= sizeof(system_settings_t))
    {
        // defaults for the fields missing in a record of an older firmware
        memcpy(&sys_settings, &sys_defaults, sizeof(system_settings_t));
        memcpy(&sys_settings, buf, size);
        if (size == sizeof(system_settings_t))
            return ESP_OK;
        ESP_LOGW(TAG, "System settings upgraded from %d to %d bytes", size, sizeof(system_settings_t));
    }
    else if (!migrate_v1(buf, size))
    {
        ESP_LOGW(TAG, "Unknown system settings format");
        return sys_settings_reset();
    }

    return sys_settings_save();
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
