#ifndef EVLAMP_SETTINGS_H_
#define EVLAMP_SETTINGS_H_

#include "common.h"

typedef enum {
    MATRIX_ROTATION_0 = 0,
    MATRIX_ROTATION_90,
    MATRIX_ROTATION_180,
    MATRIX_ROTATION_270
} matrix_rotation_t;

typedef enum {
    LED_TYPE_WS2812 = 0,
    LED_TYPE_SK6812,
    LED_TYPE_WS2811,

    LED_TYPE_MAX
} led_type_t;

typedef enum {
    LED_COLOR_ORDER_GRB = 0,
    LED_COLOR_ORDER_RGB,

    LED_COLOR_ORDER_MAX
} led_color_order_t;

#define BLE_NAME_MAX_LEN 20

typedef enum {
    WIFI_CLIENT_OFF = 0,
    WIFI_CLIENT_PERIODIC,   // connect every `period` hours for time sync and update check
    WIFI_CLIENT_ALWAYS_ON,  // stay connected, HTTP API available

    WIFI_CLIENT_MODE_MAX
} wifi_client_mode_t;

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASSWORD_MAX_LEN 64
#define WIFI_PERIOD_MAX 24

#define SYS_SETTINGS_VERSION 0x46530002 // "FS", 2; the first field of the stored record

typedef struct
{
    uint32_t version;
    struct {
        char tz[48];        // POSIX TZ string
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
    struct {
        char name[BLE_NAME_MAX_LEN + 1]; // empty: default "FireLamp-XXXX"
    } ble;
    struct {
        uint8_t mode;       // wifi_client_mode_t
        uint8_t period;     // hours, for WIFI_CLIENT_PERIODIC
        char ssid[WIFI_SSID_MAX_LEN + 1];
        char password[WIFI_PASSWORD_MAX_LEN + 1];
        bool dhcp;
        char ip[16];
        char netmask[16];
        char gateway[16];
        char dns[16];
    } wifi;

    // Append new fields only at the end of the structure: a shorter record stored by an older
    // firmware is loaded and the missing fields get default values (see sys_settings_load()).
    // When appending, set SYS_SETTINGS_MIN_SIZE in settings.c to the offset of the first new field.
} system_settings_t;

extern system_settings_t sys_settings;

// Frequently changed settings. Stored with the effect code, not the number,
// so the current effect survives firmware updates that reorder effects.
typedef struct
{
    uint8_t fps;
    uint8_t brightness;
    size_t effect;  // effect number at runtime
    bool demo;      // demo mode is on
} volatile_settings_t;

extern volatile_settings_t vol_settings;

esp_err_t settings_init();

esp_err_t sys_settings_reset();

esp_err_t sys_settings_load();

esp_err_t sys_settings_save();

esp_err_t vol_settings_reset();

esp_err_t vol_settings_load();

/**
 * Deferred: the settings are written STORAGE_SAVE_DELAY_MS after the last change
 */
esp_err_t vol_settings_save();

#endif /* EVLAMP_SETTINGS_H_ */
