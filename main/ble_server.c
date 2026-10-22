#include "ble_server.h"
#include <string.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <esp_flash.h>
#include <esp_app_desc.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include "settings.h"
#include "effect.h"
#include "surface.h"
#include "systime.h"
#include "scheduler.h"
#include "demo.h"
#include "storage.h"
#include "ota.h"
#include "bus.h"
#include "wifi.h"
#include "updater.h"
#include "input.h"

#define PROTOCOL_VERSION 1

// F1A7xxxx-B390-49C3-8770-A1DFC323D1AF, little-endian byte order
#define FL_UUID(n) BLE_UUID128_INIT(0xaf, 0xd1, 0x23, 0xc3, 0xdf, 0xa1, 0x70, 0x87, \
                                    0xc3, 0x49, 0x90, 0xb3, (n) & 0xff, (n) >> 8, 0xa7, 0xf1)

enum {
    CHR_DEVICE_INFO   = 0x0001,
    CHR_LAMP_STATE    = 0x0002,
    CHR_EFFECT_INFO   = 0x0003,
    CHR_EFFECT_PARAM  = 0x0004,
    CHR_TIME          = 0x0005,
    CHR_TIME_ZONE     = 0x0006,
    CHR_SCHEDULE      = 0x0007,
    CHR_LED_SETTINGS  = 0x0008,
    CHR_WIFI_SETTINGS = 0x0009,
    CHR_WIFI_STATUS   = 0x000a,
    CHR_DEVICE_NAME   = 0x000b,
    CHR_COMMAND       = 0x000c,
    CHR_DEMO_SETTINGS = 0x000d,
    CHR_DEMO_EFFECTS  = 0x000e,
    CHR_OTA_CONTROL   = 0x0010,
    CHR_OTA_DATA      = 0x0011,
    CHR_UPDATE_INFO   = 0x0012,
    CHR_HARDWARE      = 0x0013,
    CHR_WIFI_NETWORKS = 0x0014,
};

enum {
    STATE_MASK_ON         = BIT(0),
    STATE_MASK_EFFECT     = BIT(1),
    STATE_MASK_BRIGHTNESS = BIT(2),
    STATE_MASK_FPS        = BIT(3),
    STATE_MASK_DEMO       = BIT(4),
};

enum {
    CMD_REBOOT        = 0x01,
    CMD_RESET_EFFECTS = 0x02,
    CMD_FACTORY_RESET = 0x03,
    CMD_NEXT_EFFECT   = 0x04,
    CMD_PREV_EFFECT   = 0x05,
    CMD_CHECK_UPDATE  = 0x06,
    CMD_INSTALL_UPDATE = 0x07,
    CMD_WIFI_SCAN     = 0x08,
};

enum {
    OTA_CMD_START = 0x01,
    OTA_CMD_END   = 0x02,
    OTA_CMD_ABORT = 0x03,
};

#define BUF_SIZE 512 // max attribute value length

static const ble_uuid128_t service_uuid = FL_UUID(0x0000);

static uint16_t h_state, h_param, h_wifi_status, h_ota_control, h_update, h_networks;
static uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static bool authorized = false;
static bool sub_state, sub_param, sub_ota, sub_wifi, sub_update, sub_networks;
static uint8_t own_addr_type;
static char device_name[BLE_NAME_MAX_LEN + 1];

static int64_t pairing_until = 0;
static ble_addr_t bonded[BLE_MAX_BONDS];
static int bonded_count = 0;

static size_t effect_cursor = 0;
static size_t networks_cursor = 0; // first network of the WiFi networks page to read
static volatile bool ble_upload = false; // OTA in progress was started over BLE (not internet)

static esp_timer_handle_t auth_timer, idle_timer, state_timer, reboot_timer;

static void start_advertising();
static void idle_restart();

////////////////////////////////////////////////////////////////////////////////
/// Buffer helpers

typedef struct
{
    uint8_t data[BUF_SIZE];
    size_t len;
    bool overflow;
} buf_t;

static void put_u8(buf_t *b, uint8_t v)
{
    if (b->len + 1 > sizeof(b->data)) { b->overflow = true; return; }
    b->data[b->len++] = v;
}

static void put_u16(buf_t *b, uint16_t v)
{
    put_u8(b, v & 0xff);
    put_u8(b, v >> 8);
}

static void put_u32(buf_t *b, uint32_t v)
{
    put_u16(b, v & 0xffff);
    put_u16(b, v >> 16);
}

// s must be zero terminated, longer strings are truncated to 255 bytes
static void put_str8(buf_t *b, const char *s)
{
    size_t len = strlen(s);
    if (len > 255)
        len = 255;
    put_u8(b, len);
    if (b->len + len > sizeof(b->data)) { b->overflow = true; return; }
    memcpy(b->data + b->len, s, len);
    b->len += len;
}

typedef struct
{
    const uint8_t *data;
    size_t len, pos;
    bool error;
} reader_t;

static uint8_t get_u8(reader_t *r)
{
    if (r->pos + 1 > r->len) { r->error = true; return 0; }
    return r->data[r->pos++];
}

static uint16_t get_u16(reader_t *r)
{
    uint16_t v = get_u8(r);
    return v | (get_u8(r) << 8);
}

static uint32_t get_u32(reader_t *r)
{
    uint32_t v = get_u16(r);
    return v | ((uint32_t)get_u16(r) << 16);
}

// str8 into a zero terminated string of up to max_len characters
static void get_str8(reader_t *r, char *out, size_t max_len)
{
    size_t len = get_u8(r);
    if (r->error || len > max_len || r->pos + len > r->len) { r->error = true; out[0] = 0; return; }
    memcpy(out, r->data + r->pos, len);
    out[len] = 0;
    r->pos += len;
}

static int reply(struct ble_gatt_access_ctxt *ctxt, const buf_t *b)
{
    if (b->overflow)
        return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(ctxt->om, b->data, b->len) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Notifications

static void notify(uint16_t handle, bool subscribed, const buf_t *b)
{
    if (conn == BLE_HS_CONN_HANDLE_NONE || !authorized || !subscribed)
        return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(b->data, b->len);
    if (om)
        ble_gatts_notify_custom(conn, handle, om);
}

static void build_state(buf_t *b)
{
    put_u8(b, surface_is_playing());
    put_u8(b, vol_settings.brightness);
    put_u8(b, vol_settings.fps);
    put_u8(b, vol_settings.demo);
    put_str8(b, effects[vol_settings.effect].code);
}

static void state_timer_cb(void *arg)
{
    buf_t b = { 0 };
    build_state(&b);
    notify(h_state, sub_state, &b);
}

// surface callback, called from any task: coalesce changes
static void on_state_changed()
{
    if (state_timer && !esp_timer_is_active(state_timer))
        esp_timer_start_once(state_timer, BLE_STATE_NOTIFY_MS * 1000);
}

static void on_param_changed(size_t effect, size_t param)
{
    buf_t b = { 0 };
    put_str8(&b, effects[effect].code);
    put_str8(&b, effects[effect].params[param].code);
    put_u8(&b, effects[effect].params[param].value);
    notify(h_param, sub_param, &b);
}

static void build_wifi_status(buf_t *b);

static void on_wifi_status()
{
    buf_t b = { 0 };
    build_wifi_status(&b);
    notify(h_wifi_status, sub_wifi, &b);
}

// State, total count, first index and the networks from `first` that fit in `limit` bytes
static void build_networks(buf_t *b, size_t first, size_t limit)
{
    const wifi_network_t *nets;
    size_t count;
    wifi_scan_state_t st = wifi_scan_lock(&nets, &count);
    if (st != WIFI_SCAN_DONE)
        count = 0;
    if (first > count)
        first = count;
    put_u8(b, st);
    put_u8(b, count);
    put_u8(b, first);
    put_u8(b, 0);
    uint8_t n = 0;
    for (size_t i = first; i < count && b->len + 1 + strlen(nets[i].ssid) + 3 <= limit; i++, n++)
    {
        put_str8(b, nets[i].ssid);
        put_u8(b, (uint8_t)nets[i].rssi);
        put_u8(b, nets[i].security);
        put_u8(b, nets[i].channel);
    }
    wifi_scan_unlock();
    b->data[3] = n;
}

// WiFi task: scan started or finished. The notification has the header only,
// the app reads the list page by page.
static void on_wifi_scan()
{
    networks_cursor = 0;
    buf_t b = { 0 };
    build_networks(&b, 0, 4);
    notify(h_networks, sub_networks, &b);
}

static void on_ota_status(ota_state_t state, ota_error_t err, uint32_t received, uint32_t total)
{
    // WiFi is paused during the BLE upload
    if (state == OTA_STATE_IDLE || state == OTA_STATE_ERROR)
    {
        if (ble_upload)
            wifi_pause(false);
        ble_upload = false;
        // the update failed or was aborted: the lamp goes on as before (BUSY: another update runs)
        if (err != OTA_ERR_BUSY)
            surface_hold_for_update(false);
    }

    buf_t b = { 0 };
    put_u8(&b, state);
    put_u8(&b, err);
    put_u32(&b, received);
    put_u32(&b, total);
    notify(h_ota_control, sub_ota, &b);
}

////////////////////////////////////////////////////////////////////////////////
/// Characteristics

static int chr_device_info(struct ble_gatt_access_ctxt *ctxt)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR)
        return BLE_ATT_ERR_REQ_NOT_SUPPORTED;

    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);
    const esp_app_desc_t *app = esp_app_get_description();

    uint16_t features = BIT(2) | BIT(3); // WiFi, internet update
    if (systime_has_rtc())
        features |= BIT(0);
    if (!strcmp(storage_backend_name(), "EEPROM"))
        features |= BIT(1);

#if CONFIG_IDF_TARGET_ESP32C3
    uint8_t chip = 1;
#else
    uint8_t chip = 0;
#endif

    uint8_t controls = input_get_mode();

    char date[40];
    snprintf(date, sizeof(date), "%s %s", app->date, app->time);

    buf_t b = { 0 };
    put_u8(&b, PROTOCOL_VERSION);
    put_u8(&b, chip);
    put_u8(&b, flash >> 20);
    put_u16(&b, features);
    put_u8(&b, sys_settings.leds.width);
    put_u8(&b, sys_settings.leds.height);
    put_u8(&b, effects_count);
    put_u8(&b, controls);
    put_str8(&b, app->version);
    put_str8(&b, date);
    put_str8(&b, CONFIG_FL_BUILD_VARIANT);
    return reply(ctxt, &b);
}

static int chr_lamp_state(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        buf_t b = { 0 };
        build_state(&b);
        return reply(ctxt, &b);
    }

    uint8_t mask = get_u8(r);
    uint8_t on = get_u8(r);
    uint8_t brightness = get_u8(r);
    uint8_t fps = get_u8(r);
    uint8_t demo = get_u8(r);
    char code[EFFECT_CODE_MAX_LEN + 1];
    get_str8(r, code, EFFECT_CODE_MAX_LEN);
    if (r->error || r->pos != r->len)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    int effect = effect_find(code);
    if (((mask & STATE_MASK_EFFECT) && effect < 0)
            || ((mask & STATE_MASK_BRIGHTNESS) && brightness < BRIGHTNESS_MIN)
            || ((mask & STATE_MASK_FPS) && (!fps || fps > FPS_MAX))
            || on > 1 || demo > 1)
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;

    // manual control has priority over the dawn
    scheduler_cancel_dawn();

    if (mask & STATE_MASK_EFFECT)
    {
        if (!((mask & STATE_MASK_DEMO) && demo))
            demo_stop();
        surface_set_effect(effect);
    }
    if (mask & STATE_MASK_DEMO)
    {
        if (demo)
            demo_start();
        else
            demo_stop();
    }
    if (mask & STATE_MASK_BRIGHTNESS)
        surface_set_brightness(brightness);
    if (mask & STATE_MASK_FPS)
        surface_set_fps(fps);
    if (mask & STATE_MASK_ON)
    {
        if (on)
            surface_play();
        else
            surface_stop();
    }
    return 0;
}

static int chr_effect_info(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        uint8_t variant = get_u8(r);
        if (variant == 0x00)
        {
            uint8_t pos = get_u8(r);
            if (r->error || r->pos != r->len)
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            if (pos >= effects_count)
                return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            effect_cursor = pos;
            return 0;
        }
        if (variant == 0x01)
        {
            char code[EFFECT_CODE_MAX_LEN + 1];
            get_str8(r, code, EFFECT_CODE_MAX_LEN);
            if (r->error || r->pos != r->len)
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            int e = effect_find(code);
            if (e < 0)
                return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            effect_cursor = e;
            return 0;
        }
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }

    const effect_descriptor_t *e = &effects[effect_cursor];
    buf_t b = { 0 };
    put_u8(&b, effect_cursor);
    put_u8(&b, effects_count);
    put_str8(&b, e->code);
    put_str8(&b, e->name);
    put_u8(&b, demo_is_included(effect_cursor) ? 1 : 0);
    put_u8(&b, e->params_count);
    for (size_t p = 0; p < e->params_count; p++)
    {
        put_str8(&b, e->params[p].code);
        put_str8(&b, e->params[p].name);
        put_u8(&b, e->params[p].type);
        put_u8(&b, e->params[p].min);
        put_u8(&b, e->params[p].max);
        put_u8(&b, e->params[p].def);
        put_u8(&b, e->params[p].value);
    }
    return reply(ctxt, &b);
}

static int chr_effect_param(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        const effect_descriptor_t *e = &effects[vol_settings.effect];
        buf_t b = { 0 };
        put_str8(&b, e->code);
        put_u8(&b, e->params_count);
        for (size_t p = 0; p < e->params_count; p++)
        {
            put_str8(&b, e->params[p].code);
            put_u8(&b, e->params[p].value);
        }
        return reply(ctxt, &b);
    }

    char code[EFFECT_CODE_MAX_LEN + 1], param_code[EFFECT_CODE_MAX_LEN + 1];
    get_str8(r, code, EFFECT_CODE_MAX_LEN);
    get_str8(r, param_code, EFFECT_CODE_MAX_LEN);
    uint8_t value = get_u8(r);
    if (r->error || r->pos != r->len)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    int e = effect_find(code);
    int p = e < 0 ? -1 : effect_param_find(e, param_code);
    if (p < 0)
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    return effect_param_set(e, p, value) == ESP_OK ? 0 : BLE_ATT_ERR_VALUE_NOT_ALLOWED;
}

static int chr_time(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        buf_t b = { 0 };
        put_u32(&b, (uint32_t)time(NULL));
        put_u8(&b, systime_is_valid());
        put_u8(&b, systime_source());
        return reply(ctxt, &b);
    }

    uint32_t utc = get_u32(r);
    if (r->error || r->pos != r->len)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    return systime_set(utc, TIME_SOURCE_BLE) == ESP_OK ? 0 : BLE_ATT_ERR_VALUE_NOT_ALLOWED;
}

static int chr_time_zone(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        const char *tz = systime_get_tz();
        return os_mbuf_append(ctxt->om, tz, strlen(tz)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }

    if (!r->len || r->len >= TZ_MAX_LEN)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    char tz[TZ_MAX_LEN];
    memcpy(tz, r->data, r->len);
    tz[r->len] = 0;
    for (size_t i = 0; i < r->len; i++)
        if (tz[i] < 0x20 || tz[i] > 0x7e)
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    return systime_set_tz(tz) == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
}

static void put_schedule_time(buf_t *b, const schedule_time_t *t)
{
    put_u8(b, t->enabled);
    put_u8(b, t->days);
    put_u8(b, t->hour);
    put_u8(b, t->minute);
}

static void get_schedule_time(reader_t *r, schedule_time_t *t)
{
    t->enabled = get_u8(r);
    t->days = get_u8(r);
    t->hour = get_u8(r);
    t->minute = get_u8(r);
}

static int chr_schedule(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    schedule_t s;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        scheduler_get(&s);
        buf_t b = { 0 };
        put_schedule_time(&b, &s.power_on);
        put_schedule_time(&b, &s.power_off);
        put_schedule_time(&b, &s.dawn.at);
        put_u8(&b, s.dawn.duration);
        put_u8(&b, s.dawn.brightness);
        put_u8(&b, s.hourly.enabled);
        for (size_t h = 0; h < 24; h++)
            put_u8(&b, s.hourly.values[h]);
        put_str8(&b, s.dawn.effect);
        return reply(ctxt, &b);
    }

    memset(&s, 0, sizeof(s));
    get_schedule_time(r, &s.power_on);
    get_schedule_time(r, &s.power_off);
    get_schedule_time(r, &s.dawn.at);
    s.dawn.duration = get_u8(r);
    s.dawn.brightness = get_u8(r);
    s.hourly.enabled = get_u8(r);
    for (size_t h = 0; h < 24; h++)
        s.hourly.values[h] = get_u8(r);
    get_str8(r, s.dawn.effect, SCHEDULE_EFFECT_CODE_LEN);
    if (r->error || r->pos != r->len)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    if (s.power_on.enabled > 1 || s.power_off.enabled > 1 || s.dawn.at.enabled > 1 || s.hourly.enabled > 1
            || !schedule_validate(&s) || effect_find(s.dawn.effect) < 0)
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    return scheduler_set(&s) == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
}

static int chr_led_settings(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        buf_t b = { 0 };
        put_u8(&b, sys_settings.leds.width);
        put_u8(&b, sys_settings.leds.height);
        put_u8(&b, sys_settings.leds.type);
        put_u8(&b, sys_settings.leds.color_order);
        put_u8(&b, sys_settings.leds.rotation);
        put_u8(&b, sys_settings.leds.mirror);
        put_u16(&b, sys_settings.leds.current_limit > 0xffff ? 0xffff : sys_settings.leds.current_limit);
        put_u8(&b, sys_settings.hw.wiring);
        return reply(ctxt, &b);
    }

    uint8_t width = get_u8(r), height = get_u8(r), type = get_u8(r), order = get_u8(r);
    uint8_t rotation = get_u8(r), mirror = get_u8(r);
    uint16_t limit = get_u16(r);
    uint8_t wiring = get_u8(r);
    if (r->error || r->pos != r->len)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    if (width < MIN_MATRIX_SIZE || width > MAX_MATRIX_SIZE || height < MIN_MATRIX_SIZE || height > MAX_MATRIX_SIZE
            || width * height > MAX_MATRIX_LEDS || type >= LED_TYPE_MAX || order >= LED_COLOR_ORDER_MAX
            || rotation > MATRIX_ROTATION_270 || mirror > 1 || wiring >= MATRIX_WIRING_MAX
            || limit < width * height * CONFIG_FL_SINGLE_LED_CURRENT_MA / 50)
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;

    sys_settings.leds.width = width;
    sys_settings.leds.height = height;
    sys_settings.leds.type = type;
    sys_settings.leds.color_order = order;
    sys_settings.leds.rotation = rotation;
    sys_settings.leds.mirror = mirror;
    sys_settings.leds.current_limit = limit;
    sys_settings.hw.wiring = wiring;
    return sys_settings_save() == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
}

static int chr_hardware(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        uint8_t supported = 0;
        for (uint8_t m = 0; m < INPUT_MODE_MAX; m++)
            if (input_mode_supported(m))
                supported |= BIT(m);
        buf_t b = { 0 };
        put_u8(&b, sys_settings.hw.input_mode);
        put_u8(&b, supported);
        put_u8(&b, sys_settings.hw.button_level);
        put_u8(&b, input_main_gpio(sys_settings.hw.main_gpio));
        size_t n;
        const uint8_t *gpios = input_main_gpios(&n);
        put_u8(&b, n);
        for (size_t i = 0; i < n; i++)
            put_u8(&b, gpios[i]);
        return reply(ctxt, &b);
    }

    // 2 bytes from the apps that do not know the Main button pin: the pin is kept
    uint8_t mode = get_u8(r);
    uint8_t level = get_u8(r);
    uint8_t gpio = r->len > 2 ? get_u8(r) : input_main_gpio(sys_settings.hw.main_gpio);
    if (r->error || r->pos != r->len)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (!input_mode_supported(mode) || level > 1 || !input_main_gpio_allowed(gpio))
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    sys_settings.hw.input_mode = mode;
    sys_settings.hw.button_level = level;
    sys_settings.hw.main_gpio = gpio;
    ESP_LOGI(TAG, "Controls set to %d, buttons active %s, main button GPIO %d, applied after reboot",
            mode, level ? "high" : "low", gpio);
    return sys_settings_save() == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
}

#define PASSWORD_KEEP 0xff

static int chr_wifi_settings(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        buf_t b = { 0 };
        put_u8(&b, sys_settings.wifi.mode);
        put_u8(&b, sys_settings.wifi.period);
        put_str8(&b, sys_settings.wifi.ssid);
        // the password is never returned
        put_u8(&b, sys_settings.wifi.password[0] ? PASSWORD_KEEP : 0);
        return reply(ctxt, &b);
    }

    uint8_t mode = get_u8(r), period = get_u8(r);
    char ssid[sizeof(sys_settings.wifi.ssid)];
    get_str8(r, ssid, sizeof(ssid) - 1);
    char password[sizeof(sys_settings.wifi.password)];
    bool keep = r->pos < r->len && r->data[r->pos] == PASSWORD_KEEP;
    if (keep)
        r->pos++;
    else
        get_str8(r, password, sizeof(password) - 1);
    if (r->error || r->pos != r->len)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (mode >= WIFI_CLIENT_MODE_MAX || period < 1 || period > WIFI_PERIOD_MAX || (mode != WIFI_CLIENT_OFF && !ssid[0]))
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;

    sys_settings.wifi.mode = mode;
    sys_settings.wifi.period = period;
    strcpy(sys_settings.wifi.ssid, ssid);
    if (!keep)
        strcpy(sys_settings.wifi.password, password);
    ESP_LOGI(TAG, "WiFi client settings: mode %d, period %d h, SSID '%s'", mode, period, ssid);
    if (sys_settings_save() != ESP_OK)
        return BLE_ATT_ERR_UNLIKELY;
    wifi_apply_settings();
    return 0;
}

static void build_wifi_status(buf_t *b)
{
    wifi_status_t st;
    wifi_get_status(&st);
    put_u8(b, st.state);
    for (size_t i = 0; i < 4; i++)
        put_u8(b, st.ip[i]);
    put_u8(b, (uint8_t)st.rssi);
}

static int chr_wifi_status(struct ble_gatt_access_ctxt *ctxt)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR)
        return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    buf_t b = { 0 };
    build_wifi_status(&b);
    return reply(ctxt, &b);
}

static int chr_wifi_networks(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        buf_t b = { 0 };
        build_networks(&b, networks_cursor, BUF_SIZE);
        return reply(ctxt, &b);
    }

    // select the first network of the page to read
    uint8_t first = get_u8(r);
    if (r->error || r->pos != r->len)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    const wifi_network_t *nets;
    size_t count;
    wifi_scan_state_t st = wifi_scan_lock(&nets, &count);
    wifi_scan_unlock();
    if (st != WIFI_SCAN_DONE || first > count)
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    networks_cursor = first;
    return 0;
}

static int chr_device_name(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
        return os_mbuf_append(ctxt->om, device_name, strlen(device_name)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;

    if (r->len > BLE_NAME_MAX_LEN)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    for (size_t i = 0; i < r->len; i++)
        if (r->data[i] < 0x20 || r->data[i] == 0x7f)
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    memcpy(sys_settings.ble.name, r->data, r->len);
    sys_settings.ble.name[r->len] = 0;
    return sys_settings_save() == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
}

static void reboot_timer_cb(void *arg)
{
    esp_restart();
}

static int chr_command(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    uint8_t cmd = get_u8(r);
    if (r->error || r->pos != r->len)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    switch (cmd)
    {
        case CMD_REBOOT:
            // let the write response go out first
            esp_timer_start_once(reboot_timer, 500 * 1000);
            return 0;
        case CMD_RESET_EFFECTS:
            effects_reset();
            surface_prepare_effect(vol_settings.effect);
            return 0;
        case CMD_FACTORY_RESET:
            bus_send_event(EVENT_FACTORY_RESET, NULL, 0);
            return 0;
        case CMD_NEXT_EFFECT:
        case CMD_PREV_EFFECT:
            scheduler_cancel_dawn();
            demo_next_effect(cmd == CMD_NEXT_EFFECT ? 1 : -1);
            return 0;
        case CMD_CHECK_UPDATE:
            // WiFi must be configured
            return updater_request_check() == ESP_OK ? 0 : BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        case CMD_INSTALL_UPDATE:
            return updater_request_install() == ESP_OK ? 0 : BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        case CMD_WIFI_SCAN:
            // WiFi is paused during the BLE upload; an already running scan is not restarted
            if (ble_upload)
                return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            return wifi_scan_start() == ESP_OK ? 0 : BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        default:
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
}

static int chr_demo_settings(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    demo_settings_t s;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        demo_get_settings(&s);
        buf_t b = { 0 };
        put_u8(&b, s.order);
        put_u16(&b, s.duration);
        put_u16(&b, s.random);
        return reply(ctxt, &b);
    }

    s.order = get_u8(r);
    s.duration = get_u16(r);
    s.random = get_u16(r);
    if (r->error || r->pos != r->len)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    return demo_set_settings(&s) == ESP_OK ? 0 : BLE_ATT_ERR_VALUE_NOT_ALLOWED;
}

static int chr_demo_effects(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    char code[EFFECT_CODE_MAX_LEN + 1];
    get_str8(r, code, EFFECT_CODE_MAX_LEN);
    uint8_t include = get_u8(r);
    if (r->error || r->pos != r->len)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    int e = effect_find(code);
    if (e < 0 || include > 1)
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    return demo_set_included(e, include) == ESP_OK ? 0 : BLE_ATT_ERR_VALUE_NOT_ALLOWED;
}

static void build_update_info(buf_t *b)
{
    update_info_t i;
    updater_get_info(&i);
    put_u8(b, i.state);
    put_str8(b, i.version);
    put_u32(b, i.checked_at);
}

static int chr_update_info(struct ble_gatt_access_ctxt *ctxt)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR)
        return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    buf_t b = { 0 };
    build_update_info(&b);
    return reply(ctxt, &b);
}

static void on_update_info()
{
    buf_t b = { 0 };
    build_update_info(&b);
    notify(h_update, sub_update, &b);
}

static void request_ota_conn_params()
{
    // flash writes stall the radio from time to time: a long supervision timeout keeps the link
    struct ble_gap_upd_params params = {
        .itvl_min = 12,               // 15 ms
        .itvl_max = 24,               // 30 ms
        .latency = 0,
        .supervision_timeout = 600,   // 6 s
    };
    int rc = ble_gap_update_params(conn, &params);
    if (rc)
        ESP_LOGW(TAG, "Could not request OTA connection parameters: %d", rc);
}

static int chr_ota_control(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    uint8_t cmd = get_u8(r);
    if (r->error)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    switch (cmd)
    {
        case OTA_CMD_START:
        {
            uint32_t size = get_u32(r);
            uint8_t sha[32];
            bool with_sha = r->len - r->pos == sizeof(sha);
            if (with_sha)
            {
                memcpy(sha, r->data + r->pos, sizeof(sha));
                r->pos += sizeof(sha);
            }
            if (r->error || r->pos != r->len)
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            if (!size)
                return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            if (ota_in_progress())
            {
                on_ota_status(OTA_STATE_ERROR, OTA_ERR_BUSY, 0, 0);
                return 0;
            }
            // blank the matrix and stop automatic changes during the transfer
            scheduler_cancel_dawn();
            surface_hold_for_update(true);
            wifi_pause(true);
            request_ota_conn_params();
            ble_upload = true;
            if (ota_begin(size, with_sha ? sha : NULL) != ESP_OK)
            {
                ble_upload = false;
                wifi_pause(false);
            }
            return 0;
        }
        case OTA_CMD_END:
            if (r->pos != r->len)
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            return ota_end() == ESP_OK ? 0 : BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        case OTA_CMD_ABORT:
            ota_abort();
            return 0;
        default:
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
}

static int chr_ota_data(struct ble_gatt_access_ctxt *ctxt, reader_t *r)
{
    if (!r->len || r->len > OTA_CHUNK_MAX)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    esp_err_t res = ota_write(r->data, r->len);
    if (res == ESP_ERR_INVALID_STATE)
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    return res == ESP_OK ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (!authorized)
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;

    idle_restart();

    static uint8_t in[BUF_SIZE];
    reader_t r = { .data = in };
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        uint16_t len = 0;
        if (OS_MBUF_PKTLEN(ctxt->om) > sizeof(in))
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (ble_hs_mbuf_to_flat(ctxt->om, in, sizeof(in), &len))
            return BLE_ATT_ERR_UNLIKELY;
        r.len = len;
    }

    switch ((uintptr_t)arg)
    {
        case CHR_DEVICE_INFO:   return chr_device_info(ctxt);
        case CHR_LAMP_STATE:    return chr_lamp_state(ctxt, &r);
        case CHR_EFFECT_INFO:   return chr_effect_info(ctxt, &r);
        case CHR_EFFECT_PARAM:  return chr_effect_param(ctxt, &r);
        case CHR_TIME:          return chr_time(ctxt, &r);
        case CHR_TIME_ZONE:     return chr_time_zone(ctxt, &r);
        case CHR_SCHEDULE:      return chr_schedule(ctxt, &r);
        case CHR_LED_SETTINGS:  return chr_led_settings(ctxt, &r);
        case CHR_WIFI_SETTINGS: return chr_wifi_settings(ctxt, &r);
        case CHR_WIFI_STATUS:   return chr_wifi_status(ctxt);
        case CHR_DEVICE_NAME:   return chr_device_name(ctxt, &r);
        case CHR_COMMAND:       return chr_command(ctxt, &r);
        case CHR_DEMO_SETTINGS: return chr_demo_settings(ctxt, &r);
        case CHR_DEMO_EFFECTS:  return chr_demo_effects(ctxt, &r);
        case CHR_OTA_CONTROL:   return chr_ota_control(ctxt, &r);
        case CHR_OTA_DATA:      return chr_ota_data(ctxt, &r);
        case CHR_UPDATE_INFO:   return chr_update_info(ctxt);
        case CHR_HARDWARE:      return chr_hardware(ctxt, &r);
        case CHR_WIFI_NETWORKS: return chr_wifi_networks(ctxt, &r);
        default:                return BLE_ATT_ERR_UNLIKELY;
    }
}

////////////////////////////////////////////////////////////////////////////////
/// GATT table

#define R  (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC)
#define W  (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC)
#define N  (BLE_GATT_CHR_F_NOTIFY)

#define CHR(NUM, FLAGS) { .uuid = &(const ble_uuid128_t)FL_UUID(NUM).u, .access_cb = access_cb, .arg = (void *)(NUM), .flags = (FLAGS) }
#define CHR_H(NUM, FLAGS, HANDLE) { .uuid = &(const ble_uuid128_t)FL_UUID(NUM).u, .access_cb = access_cb, .arg = (void *)(NUM), .flags = (FLAGS), .val_handle = (HANDLE) }

static const struct ble_gatt_svc_def services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            CHR(CHR_DEVICE_INFO, R),
            CHR_H(CHR_LAMP_STATE, R | W | N, &h_state),
            CHR(CHR_EFFECT_INFO, R | W),
            CHR_H(CHR_EFFECT_PARAM, R | W | N, &h_param),
            CHR(CHR_TIME, R | W),
            CHR(CHR_TIME_ZONE, R | W),
            CHR(CHR_SCHEDULE, R | W),
            CHR(CHR_LED_SETTINGS, R | W),
            CHR(CHR_WIFI_SETTINGS, R | W),
            CHR_H(CHR_WIFI_STATUS, R | N, &h_wifi_status),
            CHR(CHR_DEVICE_NAME, R | W),
            CHR(CHR_COMMAND, W),
            CHR(CHR_DEMO_SETTINGS, R | W),
            CHR(CHR_DEMO_EFFECTS, W),
            CHR_H(CHR_OTA_CONTROL, W | N, &h_ota_control),
            CHR(CHR_OTA_DATA, W),
            CHR_H(CHR_UPDATE_INFO, R | N, &h_update),
            CHR(CHR_HARDWARE, R | W),
            CHR_H(CHR_WIFI_NETWORKS, R | W | N, &h_networks),
            { 0 }
        },
    },
    { 0 }
};

////////////////////////////////////////////////////////////////////////////////
/// Security

static void load_bonded()
{
    if (ble_store_util_bonded_peers(bonded, &bonded_count, BLE_MAX_BONDS))
        bonded_count = 0;
    ESP_LOGI(TAG, "BLE: %d bonded phone(s)", bonded_count);
}

static bool is_bonded(const ble_addr_t *addr)
{
    for (int i = 0; i < bonded_count; i++)
        if (!ble_addr_cmp(&bonded[i], addr))
            return true;
    return false;
}

static bool pairing_open()
{
    return esp_timer_get_time() < pairing_until;
}

// Bond store is full and new keys are coming: make room by forgetting the oldest phone,
// but only inside the pairing window, so a stranger cannot evict bonded phones
static int store_status(struct ble_store_status_event *event, void *arg)
{
    if ((event->event_code == BLE_STORE_EVENT_OVERFLOW || event->event_code == BLE_STORE_EVENT_FULL)
            && !pairing_open())
    {
        ESP_LOGW(TAG, "BLE: bond store is full, pairing window is closed: keys rejected");
        return BLE_HS_ESTORE_CAP;
    }
    return ble_store_util_status_rr(event, arg);
}

static void auth_timer_cb(void *arg)
{
    if (conn != BLE_HS_CONN_HANDLE_NONE && !authorized)
    {
        ESP_LOGW(TAG, "BLE: phone did not pair in time, disconnecting");
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }
}

// Android keeps the link to a just bonded lamp open with no app using it, and the lamp
// cannot advertise while connected. Drop a connection that nobody uses.
static void idle_timer_cb(void *arg)
{
    if (conn == BLE_HS_CONN_HANDLE_NONE)
        return;
    // an app waiting for notifications is not idle
    if (sub_state || sub_param || sub_ota || sub_wifi || sub_update || sub_networks)
    {
        idle_restart();
        return;
    }
    ESP_LOGW(TAG, "BLE: connection is idle for %d s, disconnecting", BLE_IDLE_TIMEOUT_S);
    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}

static void idle_restart()
{
    esp_timer_stop(idle_timer);
    esp_timer_start_once(idle_timer, BLE_IDLE_TIMEOUT_S * 1000000ULL);
}

// Encryption is established: either with an existing bond or after a new pairing
static void on_encrypted(uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn_handle, &desc))
        return;

    if (is_bonded(&desc.peer_id_addr))
    {
        authorized = true;
        ESP_LOGI(TAG, "BLE: bonded phone connected");
    }
    else if (pairing_open())
    {
        authorized = true;
        ESP_LOGI(TAG, "BLE: new phone paired");
        load_bonded();
    }
    else
    {
        // a new pairing outside the window: forget the keys and drop the phone
        ESP_LOGW(TAG, "BLE: pairing rejected, pairing window is closed");
        ble_gap_unpair(&desc.peer_id_addr);
        return;
    }
    esp_timer_stop(auth_timer);
}

////////////////////////////////////////////////////////////////////////////////
/// GAP

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status)
            {
                start_advertising();
                break;
            }
            conn = event->connect.conn_handle;
            authorized = false;
            sub_state = sub_param = sub_ota = sub_wifi = sub_update = sub_networks = false;
            effect_cursor = 0;
            networks_cursor = 0;
            ESP_LOGI(TAG, "BLE: connected");
            esp_timer_start_once(auth_timer, BLE_AUTH_TIMEOUT_S * 1000000ULL);
            idle_restart();
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE: disconnected, reason 0x%x", event->disconnect.reason);
            conn = BLE_HS_CONN_HANDLE_NONE;
            authorized = false;
            esp_timer_stop(auth_timer);
            esp_timer_stop(idle_timer);
            // an interrupted BLE upload is useless; an internet update goes on without the phone
            if (ble_upload)
                ota_abort();
            start_advertising();
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            start_advertising();
            break;

        case BLE_GAP_EVENT_ENC_CHANGE:
            if (event->enc_change.status == 0)
                on_encrypted(event->enc_change.conn_handle);
            else
                ESP_LOGW(TAG, "BLE: encryption failed: %d", event->enc_change.status);
            break;

        case BLE_GAP_EVENT_REPEAT_PAIRING:
        {
            // the phone lost its keys and pairs again
            if (!pairing_open())
                return BLE_GAP_REPEAT_PAIRING_IGNORE;
            struct ble_gap_conn_desc desc;
            if (!ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc))
                ble_store_util_delete_peer(&desc.peer_id_addr);
            load_bonded();
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }

        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == h_state)
                sub_state = event->subscribe.cur_notify;
            else if (event->subscribe.attr_handle == h_param)
                sub_param = event->subscribe.cur_notify;
            else if (event->subscribe.attr_handle == h_ota_control)
                sub_ota = event->subscribe.cur_notify;
            else if (event->subscribe.attr_handle == h_wifi_status)
                sub_wifi = event->subscribe.cur_notify;
            else if (event->subscribe.attr_handle == h_update)
                sub_update = event->subscribe.cur_notify;
            else if (event->subscribe.attr_handle == h_networks)
                sub_networks = event->subscribe.cur_notify;
            else
                break; // not ours, e.g. Service Changed subscribed by the phone's system
            idle_restart();
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "BLE: MTU %d", event->mtu.value);
            break;

        default:
            break;
    }
    return 0;
}

static void start_advertising()
{
    struct ble_hs_adv_fields fields = { 0 };
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc)
    {
        ESP_LOGE(TAG, "BLE: could not set advertising data: %d", rc);
        return;
    }

    // the name does not fit next to the 128 bit UUID, it goes to the scan response
    struct ble_hs_adv_fields rsp = { 0 };
    rsp.name = (const uint8_t *)device_name;
    rsp.name_len = strlen(device_name);
    rsp.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc)
    {
        ESP_LOGE(TAG, "BLE: could not set scan response data: %d", rc);
        return;
    }

    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc && rc != BLE_HS_EALREADY)
        ESP_LOGE(TAG, "BLE: could not start advertising: %d", rc);
}

static void on_sync()
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &own_addr_type);
    load_bonded();
    // Bonded phones cache the attribute table, and a firmware update may add characteristics.
    // NimBLE keeps this "Service Changed" for every bonded phone and indicates it on the next
    // connection, so the phone discovers the services again. Done on every boot: cheap, and
    // works for any update path (BLE, internet, USB).
    ble_svc_gatt_changed(0x0001, 0xffff);
    start_advertising();
    ESP_LOGI(TAG, "BLE: advertising as '%s'", device_name);
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE: host reset, reason %d", reason);
}

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_store_config_init(void);

////////////////////////////////////////////////////////////////////////////////

void ble_server_open_pairing_window()
{
    pairing_until = esp_timer_get_time() + BLE_PAIRING_WINDOW_S * 1000000LL;
    ESP_LOGI(TAG, "BLE: pairing window is open for %d s", BLE_PAIRING_WINDOW_S);
}

void ble_server_forget_bonds()
{
    ble_store_clear();
    bonded_count = 0;
    ESP_LOGW(TAG, "BLE: all bonded phones forgotten");
}

esp_err_t ble_server_init()
{
    if (sys_settings.ble.name[0])
        snprintf(device_name, sizeof(device_name), "%s", sys_settings.ble.name);
    else
    {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_BT);
        snprintf(device_name, sizeof(device_name), APP_NAME "-%02X%02X", mac[4], mac[5]);
    }

    esp_timer_create_args_t args = { .callback = auth_timer_cb, .name = "ble_auth" };
    CHECK(esp_timer_create(&args, &auth_timer));
    args = (esp_timer_create_args_t) { .callback = idle_timer_cb, .name = "ble_idle" };
    CHECK(esp_timer_create(&args, &idle_timer));
    args = (esp_timer_create_args_t) { .callback = state_timer_cb, .name = "ble_state" };
    CHECK(esp_timer_create(&args, &state_timer));
    args = (esp_timer_create_args_t) { .callback = reboot_timer_cb, .name = "ble_reboot" };
    CHECK(esp_timer_create(&args, &reboot_timer));

    CHECK(ota_init(on_ota_status));

    CHECK_LOGE(nimble_port_init(), "Could not init NimBLE");

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = store_status;

    // "Just Works" pairing with bonding, LE Secure Connections
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(services);
    if (!rc)
        rc = ble_gatts_add_svcs(services);
    if (rc)
    {
        ESP_LOGE(TAG, "BLE: could not register GATT service: %d", rc);
        return ESP_FAIL;
    }
    ble_svc_gap_device_name_set(device_name);
    ble_store_config_init();

    surface_set_state_cb(on_state_changed);
    effect_set_param_cb(on_param_changed);
    wifi_set_status_cb(on_wifi_status);
    wifi_set_scan_cb(on_wifi_scan);
    updater_set_cb(on_update_info);

    // new phones can pair right after power on
    ble_server_open_pairing_window();

    nimble_port_freertos_init(host_task);

    return ESP_OK;
}
