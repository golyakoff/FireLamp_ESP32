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
 * Callback called when the WiFi state changes. Called from different tasks, must be fast.
 */
void wifi_set_status_cb(wifi_status_cb_t cb);

#endif /* EVLAMP_WIFI_H_ */
