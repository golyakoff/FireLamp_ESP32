#ifndef EVLAMP_OTA_H_
#define EVLAMP_OTA_H_

#include "common.h"

/**
 * Firmware update writer, used by BLE upload (and internet update later).
 * Flash operations run in a separate task, the API only queues requests.
 */

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_RECEIVING,
    OTA_STATE_SUCCESS,   // the lamp reboots in OTA_REBOOT_DELAY_MS
    OTA_STATE_ERROR,
    OTA_STATE_CHECKING,  // internet: checking for update
    OTA_STATE_NO_UPDATE, // internet: no update available
} ota_state_t;

typedef enum {
    OTA_ERR_NONE = 0,
    OTA_ERR_BUSY,
    OTA_ERR_SIZE,
    OTA_ERR_FLASH,
    OTA_ERR_IMAGE,
    OTA_ERR_SHA,
    OTA_ERR_TIMEOUT,
    OTA_ERR_OVERFLOW,
    OTA_ERR_DOWNLOAD,
} ota_error_t;

#define OTA_CHUNK_MAX 514 // MTU 517 - 3

typedef void (*ota_status_cb_t)(ota_state_t state, ota_error_t err, uint32_t received, uint32_t total);

esp_err_t ota_init(ota_status_cb_t cb);

/**
 * Start update. The inactive OTA partition is erased in the background, then
 * the status callback reports OTA_STATE_RECEIVING.
 *
 * @param size Firmware size
 * @param sha256 Expected SHA-256 of the firmware file or NULL
 * @return ESP_ERR_INVALID_STATE if an update is already in progress
 */
esp_err_t ota_begin(uint32_t size, const uint8_t *sha256);

/**
 * Queue next chunk. Blocks while the queue is full (flow control).
 */
esp_err_t ota_write(const void *data, size_t len);

/**
 * All data sent: verify, switch boot partition and reboot
 */
esp_err_t ota_end();

void ota_abort();

/**
 * Abort with an error reported to the status callback (internet download failed etc)
 */
void ota_fail(ota_error_t err);

/**
 * Wait until the partition is erased and data can be written (after ota_begin())
 */
esp_err_t ota_wait_ready(uint32_t timeout_ms);

/**
 * Report a state to the status callback (internet update: checking, no update)
 */
void ota_report(ota_state_t state, ota_error_t err);

bool ota_in_progress();

/**
 * Firmware in the other OTA slot, the one the lamp can go back to.
 *
 * @param version Its version, e.g. `0.6.0`
 * @return false if the slot holds no firmware to go back to: never written (flashed over USB),
 *         erased by an update, or failed to start before
 */
bool ota_previous_version(char *version, size_t size);

/**
 * Boot the firmware of the other OTA slot next time (see ota_previous_version()). The caller
 * reboots.
 *
 * @return ESP_ERR_NOT_FOUND if there is none, ESP_ERR_INVALID_STATE during an update
 */
esp_err_t ota_rollback();

#endif /* EVLAMP_OTA_H_ */
