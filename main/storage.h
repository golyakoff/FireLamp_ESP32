#ifndef EVLAMP_STORAGE_H_
#define EVLAMP_STORAGE_H_

#include "common.h"

/**
 * Settings storage: named binary records in NVS or in external AT24Cxx EEPROM
 * (selected in menuconfig). Each record carries magic, size and CRC.
 * In EEPROM every record has two slots written alternately, so a power loss
 * during write keeps the previous copy.
 */

typedef enum {
    STORAGE_SYSTEM = 0,
    STORAGE_VOLATILE,
    STORAGE_EFFECTS,
    STORAGE_SCHEDULE,
    STORAGE_DEMO,
    // Append new records only: EEPROM layout depends on the order and sizes

    STORAGE_RECORD_MAX
} storage_record_t;

#define STORAGE_SYSTEM_MAX_SIZE   768
#define STORAGE_VOLATILE_MAX_SIZE 64
#define STORAGE_EFFECTS_MAX_SIZE  640
#define STORAGE_SCHEDULE_MAX_SIZE 128
#define STORAGE_DEMO_MAX_SIZE     128

typedef esp_err_t (*storage_saver_t)();

/**
 * Initialize NVS (always, WiFi needs it) and the selected backend.
 * Falls back to NVS when the EEPROM is not responding.
 */
esp_err_t storage_init();

const char *storage_backend_name();

/**
 * Load record
 *
 * @param rec Record
 * @param data Target buffer
 * @param[in,out] size Buffer size in, actual record size out
 * @return ESP_ERR_NOT_FOUND if there is no valid record
 */
esp_err_t storage_load(storage_record_t rec, void *data, size_t *size);

/**
 * Save record immediately
 */
esp_err_t storage_save(storage_record_t rec, const void *data, size_t size);

/**
 * Schedule saving: `saver` will be called once after the record has not been
 * changed for STORAGE_SAVE_DELAY_MS, or on restart. Saves EEPROM/flash wear
 * when values change rapidly (brightness slider etc).
 */
void storage_defer(storage_record_t rec, storage_saver_t saver);

/**
 * Run all pending savers now
 */
esp_err_t storage_flush();

#endif /* EVLAMP_STORAGE_H_ */
