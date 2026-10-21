#ifndef EVLAMP_WIFI_H_
#define EVLAMP_WIFI_H_

#include "common.h"

/**
 * WiFi client (station) with three modes, see wifi_client_mode_t:
 *  - off;
 *  - periodic: every `period` hours connect, sync time by SNTP, disconnect;
 *  - always on: stay connected, SNTP and HTTP API work.
 * Generates EVENT_NETWORK_UP / EVENT_NETWORK_DOWN bus events.
 */

typedef enum {
    WIFI_STATE_OFF = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_ERROR,      // wrong password, network not found
} wifi_state_t;

typedef struct
{
    uint8_t state;   // wifi_state_t
    uint8_t ip[4];   // zeros if not connected
    int8_t rssi;     // dBm, 0 if not connected
} wifi_status_t;

typedef void (*wifi_status_cb_t)();

typedef enum {
    WIFI_SCAN_NONE = 0,  // not scanned since boot
    WIFI_SCAN_RUNNING,
    WIFI_SCAN_DONE,
    WIFI_SCAN_ERROR,
} wifi_scan_state_t;

typedef enum {
    WIFI_SECURITY_OPEN = 0,      // also OWE
    WIFI_SECURITY_WEP,
    WIFI_SECURITY_PERSONAL,      // WPA/WPA2/WPA3 personal, WAPI
    WIFI_SECURITY_ENTERPRISE,    // the lamp cannot connect to it
} wifi_security_t;

typedef struct
{
    char ssid[33];
    int8_t rssi;       // dBm
    uint8_t security;  // wifi_security_t
    uint8_t channel;
} wifi_network_t;

typedef void (*wifi_scan_cb_t)();

/**
 * Initialize network interface and WiFi driver, start the WiFi task
 */
esp_err_t wifi_init();

/**
 * WiFi settings changed: reconnect according to the new settings
 */
void wifi_apply_settings();

/**
 * Temporarily stop WiFi (BLE firmware upload) and resume it
 */
void wifi_pause(bool pause);

void wifi_get_status(wifi_status_t *status);

/**
 * Something needs the internet (update check / install): in the periodic mode start a session
 * now, in the always-on mode run it as soon as connected
 */
void wifi_request_session();

/**
 * Callback called when the WiFi state changes. Called from different tasks, must be fast.
 */
void wifi_set_status_cb(wifi_status_cb_t cb);

/**
 * Scan for networks in any WiFi mode, asynchronously. When WiFi is off (or between the sessions
 * of the periodic mode) the radio is started for the scan only; an always-on connection is kept.
 * During a periodic session the scan runs right after it.
 *
 * @return ESP_OK if started or already running, ESP_ERR_INVALID_STATE if WiFi is not available
 */
esp_err_t wifi_scan_start();

/**
 * Lock the scan result and get it: unique SSIDs without hidden networks, strongest first.
 * Must be followed by wifi_scan_unlock().
 */
wifi_scan_state_t wifi_scan_lock(const wifi_network_t **networks, size_t *count);

void wifi_scan_unlock();

/**
 * Callback called when the scan starts and finishes. Called from different tasks.
 */
void wifi_set_scan_cb(wifi_scan_cb_t cb);

#endif /* EVLAMP_WIFI_H_ */
