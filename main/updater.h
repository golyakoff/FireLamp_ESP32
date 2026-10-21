#ifndef EVLAMP_UPDATER_H_
#define EVLAMP_UPDATER_H_

#include "common.h"

/**
 * Firmware update from the internet (GitHub releases).
 *
 * The lamp reads the release manifest (CONFIG_FL_OTA_MANIFEST_URL) on every WiFi session
 * and on request, and reports whether a newer version is available. The update is installed
 * only on request (app command). The firmware is written by the ota module, so the progress is
 * reported the same way as for a BLE upload.
 *
 * Manifest format:
 * {
 *   "version": "0.7.0",
 *   "builds": {
 *     "esp32-default":   { "url": "https://...bin", "size": 1234567, "sha256": "<64 hex chars>" },
 *     "esp32c3-default": { ... }
 *   }
 * }
 * Build key: CONFIG_IDF_TARGET "-" CONFIG_FL_BUILD_VARIANT. A lamp whose build is not in
 * the manifest is reported as up to date.
 */

typedef enum {
    UPDATE_UNKNOWN = 0, // not checked since boot
    UPDATE_CHECKING,
    UPDATE_UP_TO_DATE,
    UPDATE_AVAILABLE,
    UPDATE_CHECK_FAILED,
} update_state_t;

typedef struct
{
    uint8_t state;          // update_state_t
    char version[32];       // available version (UPDATE_AVAILABLE)
    uint32_t checked_at;    // time of the last successful check, UTC, 0 if unknown
} update_info_t;

typedef void (*updater_cb_t)();

void updater_get_info(update_info_t *info);

/**
 * Callback called when the update info changes
 */
void updater_set_cb(updater_cb_t cb);

/**
 * Request a check / an installation. Runs on the next WiFi connection (WiFi connects right
 * away in the periodic mode).
 *
 * @return ESP_ERR_INVALID_STATE if WiFi is off or not configured
 */
esp_err_t updater_request_check();

esp_err_t updater_request_install();

/**
 * Called by the WiFi task while connected: performs the requested or the regular check
 * and the requested installation
 *
 * @param regular_check The regular check of a WiFi session is due
 */
void updater_run(bool regular_check);

/**
 * Something is requested
 */
bool updater_pending();

/**
 * Called by the WiFi task when it could not connect: fails the pending requests,
 * so the app does not wait for an answer that never comes
 */
void updater_fail_pending();

#endif /* EVLAMP_UPDATER_H_ */
