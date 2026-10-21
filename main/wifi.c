#include "wifi.h"

#include <string.h>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <esp_timer.h>
#include <lwip/ip_addr.h>
#include "settings.h"
#include "systime.h"
#include "bus.h"

#define BIT_GOT_IP       BIT(0)
#define BIT_DISCONNECTED BIT(1)
#define BIT_REQUEST      BIT(2) // settings changed, pause/resume

static esp_netif_t *iface = NULL;
static EventGroupHandle_t events = NULL;
static wifi_status_cb_t status_cb = NULL;

static volatile wifi_state_t state = WIFI_STATE_OFF;
static volatile uint32_t ip_addr = 0;
static volatile bool paused = false;
static volatile bool want_connect = false; // reconnect on STA_START
static volatile uint8_t last_reason = 0;
static bool started = false;
static bool sntp_running = false;

static void set_state(wifi_state_t s)
{
    if (state == s)
        return;
    state = s;
    if (status_cb)
        status_cb();
}

static bool is_error_reason(uint8_t reason)
{
    switch (reason)
    {
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_NO_AP_FOUND:
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
            return true;
        default:
            return false;
    }
}

static void wifi_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id)
    {
        case WIFI_EVENT_STA_START:
            if (want_connect)
                esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_event_sta_disconnected_t *e = data;
            last_reason = e->reason;
            bool was_connected = ip_addr != 0;
            ip_addr = 0;
            ESP_LOGI(TAG, "WiFi disconnected, reason %d", e->reason);
            set_state(started && is_error_reason(e->reason) ? WIFI_STATE_ERROR
                    : started ? WIFI_STATE_CONNECTING : WIFI_STATE_OFF);
            xEventGroupSetBits(events, BIT_DISCONNECTED);
            if (was_connected)
                bus_send_event(EVENT_NETWORK_DOWN, NULL, 0);
            break;
        }
        default:
            break;
    }
}

static void ip_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id != IP_EVENT_STA_GOT_IP)
        return;
    ip_event_got_ip_t *e = data;
    ip_addr = e->ip_info.ip.addr;
    ESP_LOGI(TAG, "WiFi connected, IP " IPSTR, IP2STR(&e->ip_info.ip));
    set_state(WIFI_STATE_CONNECTED);
    xEventGroupSetBits(events, BIT_GOT_IP);
    bus_send_event(EVENT_NETWORK_UP, NULL, 0);
}

static void sntp_synced(struct timeval *tv)
{
    systime_set(tv->tv_sec, TIME_SOURCE_SNTP);
}

static void sntp_start()
{
    if (sntp_running)
        return;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_FL_SNTP_SERVER);
    cfg.sync_cb = sntp_synced;
    if (esp_netif_sntp_init(&cfg) == ESP_OK)
        sntp_running = true;
}

static void sntp_stop()
{
    if (!sntp_running)
        return;
    esp_netif_sntp_deinit();
    sntp_running = false;
}

static void set_static_ip()
{
    esp_netif_dhcpc_stop(iface);

    esp_netif_ip_info_t info = {
        .ip.addr = ipaddr_addr(sys_settings.wifi.ip),
        .netmask.addr = ipaddr_addr(sys_settings.wifi.netmask),
        .gw.addr = ipaddr_addr(sys_settings.wifi.gateway),
    };
    esp_err_t res = esp_netif_set_ip_info(iface, &info);
    if (res != ESP_OK)
        ESP_LOGW(TAG, "Error setting static IP address: %d (%s)", res, esp_err_to_name(res));

    esp_netif_dns_info_t dns = { .ip.type = IPADDR_TYPE_V4 };
    dns.ip.u_addr.ip4.addr = ipaddr_addr(sys_settings.wifi.dns);
    esp_netif_set_dns_info(iface, ESP_NETIF_DNS_MAIN, &dns);
}

static esp_err_t start()
{
    wifi_config_t cfg = { 0 };
    // 32 byte SSID / 64 byte password fill the fields without a terminating zero, that is valid
    memcpy(cfg.sta.ssid, sys_settings.wifi.ssid, strnlen(sys_settings.wifi.ssid, sizeof(cfg.sta.ssid)));
    memcpy(cfg.sta.password, sys_settings.wifi.password, strnlen(sys_settings.wifi.password, sizeof(cfg.sta.password)));
    cfg.sta.threshold.authmode = sys_settings.wifi.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    if (sys_settings.wifi.dhcp)
        esp_netif_dhcpc_start(iface);
    else
        set_static_ip();

    CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    ESP_LOGI(TAG, "WiFi: connecting to '%s'", sys_settings.wifi.ssid);
    xEventGroupClearBits(events, BIT_GOT_IP | BIT_DISCONNECTED);
    want_connect = true;
    started = true;
    set_state(WIFI_STATE_CONNECTING);
    return esp_wifi_start();
}

static void stop()
{
    if (!started)
        return;
    sntp_stop();
    want_connect = false;
    started = false;
    esp_wifi_stop();
    ip_addr = 0;
    set_state(WIFI_STATE_OFF);
    ESP_LOGI(TAG, "WiFi: stopped");
}

static wifi_client_mode_t current_mode()
{
    if (paused || !sys_settings.wifi.ssid[0])
        return WIFI_CLIENT_OFF;
    return sys_settings.wifi.mode;
}

// Wait for any of the bits or timeout, returns the bits that were set
static EventBits_t wait(EventBits_t bits, uint32_t timeout_ms)
{
    return xEventGroupWaitBits(events, bits, pdTRUE, pdFALSE,
            timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms));
}

// Returns false if WiFi could not be started at all
static bool run_always_on()
{
    uint32_t backoff = WIFI_RECONNECT_MIN_MS;
    esp_err_t res = start();
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "WiFi: could not start: %d (%s)", res, esp_err_to_name(res));
        stop();
        set_state(WIFI_STATE_ERROR);
        return false;
    }
    while (current_mode() == WIFI_CLIENT_ALWAYS_ON)
    {
        EventBits_t b = wait(BIT_GOT_IP | BIT_DISCONNECTED | BIT_REQUEST, UINT32_MAX);
        if (b & BIT_REQUEST)
            break;
        if (b & BIT_GOT_IP)
        {
            backoff = WIFI_RECONNECT_MIN_MS;
            sntp_start();
        }
        if (b & BIT_DISCONNECTED)
        {
            // reconnect later, unless the settings change meanwhile
            if (wait(BIT_REQUEST, backoff) & BIT_REQUEST)
                break;
            backoff = MIN(backoff * 2, WIFI_RECONNECT_MAX_MS);
            esp_wifi_connect();
        }
    }
    stop();
    return true;
}

// One session of the periodic mode, returns true on success
static bool run_session()
{
    if (start() != ESP_OK)
        return false;

    bool ok = false;
    int64_t deadline = esp_timer_get_time() + WIFI_SESSION_CONNECT_MS * 1000LL;
    while (esp_timer_get_time() < deadline)
    {
        EventBits_t b = wait(BIT_GOT_IP | BIT_DISCONNECTED | BIT_REQUEST,
                (deadline - esp_timer_get_time()) / 1000 + 1);
        if (b & (BIT_REQUEST | BIT_GOT_IP))
        {
            ok = b & BIT_GOT_IP;
            break;
        }
        if (b & BIT_DISCONNECTED)
        {
            if (is_error_reason(last_reason))
                break;
            esp_wifi_connect();
        }
    }

    if (ok)
    {
        sntp_start();
        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(WIFI_SNTP_WAIT_MS)) != ESP_OK)
            ESP_LOGW(TAG, "WiFi: time sync timeout");
        // TODO stage 7: check for firmware update here
    }

    wifi_state_t result = state;
    stop();
    // keep the error visible until the next session
    if (!ok)
        set_state(result == WIFI_STATE_ERROR ? WIFI_STATE_ERROR : WIFI_STATE_OFF);
    return ok;
}

static void wifi_task(void *arg)
{
    int64_t next_session = esp_timer_get_time() + WIFI_FIRST_SESSION_DELAY_MS * 1000LL;

    while (1)
    {
        switch (current_mode())
        {
            case WIFI_CLIENT_ALWAYS_ON:
                if (!run_always_on())
                    wait(BIT_REQUEST, WIFI_RECONNECT_MAX_MS);
                break;

            case WIFI_CLIENT_PERIODIC:
            {
                int64_t now = esp_timer_get_time();
                if (now >= next_session)
                {
                    bool ok = run_session();
                    uint64_t delay_ms = ok
                        ? sys_settings.wifi.period * 3600000ULL
                        : WIFI_SESSION_RETRY_MS;
                    next_session = esp_timer_get_time() + delay_ms * 1000LL;
                    ESP_LOGI(TAG, "WiFi: next session in %llu min", delay_ms / 60000);
                    break;
                }
                // a settings change starts a new session soon
                if (wait(BIT_REQUEST, (next_session - now) / 1000 + 1) & BIT_REQUEST)
                    next_session = esp_timer_get_time() + WIFI_FIRST_SESSION_DELAY_MS * 1000LL;
                break;
            }

            default:
                stop();
                if (state != WIFI_STATE_OFF)
                    set_state(WIFI_STATE_OFF);
                wait(BIT_REQUEST, UINT32_MAX);
                next_session = esp_timer_get_time() + WIFI_FIRST_SESSION_DELAY_MS * 1000LL;
                break;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

esp_err_t wifi_init()
{
    events = xEventGroupCreate();
    if (!events)
        return ESP_ERR_NO_MEM;

    CHECK(esp_netif_init());
    CHECK(esp_event_loop_create_default());

    iface = esp_netif_create_default_wifi_sta();
    CHECK(esp_netif_set_hostname(iface, WIFI_HOSTNAME));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    CHECK(esp_wifi_init(&cfg));
    // the settings are kept by the lamp, do not wear the NVS
    CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_handler, NULL, NULL));
    CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_handler, NULL, NULL));

    if (xTaskCreate(wifi_task, "wifi", WIFI_CLIENT_TASK_STACK_SIZE, NULL, WIFI_CLIENT_TASK_PRIORITY, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "WiFi mode: %d, SSID '%s'", sys_settings.wifi.mode, sys_settings.wifi.ssid);

    return ESP_OK;
}

void wifi_apply_settings()
{
    if (events)
        xEventGroupSetBits(events, BIT_REQUEST);
}

void wifi_pause(bool pause)
{
    if (paused == pause)
        return;
    paused = pause;
    ESP_LOGI(TAG, "WiFi %s", pause ? "paused" : "resumed");
    wifi_apply_settings();
}

void wifi_get_status(wifi_status_t *s)
{
    memset(s, 0, sizeof(wifi_status_t));
    s->state = state;
    uint32_t addr = ip_addr;
    if (state == WIFI_STATE_CONNECTED && addr)
    {
        memcpy(s->ip, &addr, 4); // network byte order
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
            s->rssi = ap.rssi;
    }
}

void wifi_set_status_cb(wifi_status_cb_t cb)
{
    status_cb = cb;
}
