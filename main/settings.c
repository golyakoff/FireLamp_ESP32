#include "settings.h"
#include <string.h>
#include "storage.h"
#include "surface.h"

_Static_assert(sizeof(system_settings_t) <= STORAGE_SYSTEM_MAX_SIZE, "System settings do not fit storage record");
_Static_assert(sizeof(volatile_settings_t) <= STORAGE_VOLATILE_MAX_SIZE, "Volatile settings do not fit storage record");

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

static const volatile_settings_t vol_defaults = {
    .fps        = CONFIG_FL_FPS_DEFAULT,
    .brightness = CONFIG_FL_BRIGHTNESS_DEFAULT,
    .effect     = CONFIG_FL_EFFECT_DEFAULT,
};

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
    return storage_save(STORAGE_VOLATILE, &vol_settings, sizeof(volatile_settings_t));
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

esp_err_t sys_settings_load()
{
    if (load(STORAGE_SYSTEM, &sys_settings, sizeof(system_settings_t)) != ESP_OK)
        return sys_settings_reset();

    return ESP_OK;
}

esp_err_t sys_settings_save()
{
    return storage_save(STORAGE_SYSTEM, &sys_settings, sizeof(system_settings_t));
}

esp_err_t vol_settings_reset()
{
    memcpy(&vol_settings, &vol_defaults, sizeof(volatile_settings_t));
    CHECK(vol_settings_save_now());

    ESP_LOGW(TAG, "Volatile settings have been reset to defaults");

    return ESP_OK;
}

esp_err_t vol_settings_load()
{
    if (load(STORAGE_VOLATILE, &vol_settings, sizeof(volatile_settings_t)) != ESP_OK)
        return vol_settings_reset();

    return ESP_OK;
}

esp_err_t vol_settings_save()
{
    storage_defer(STORAGE_VOLATILE, vol_settings_save_now);
    return ESP_OK;
}
