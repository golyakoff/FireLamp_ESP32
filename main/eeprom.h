#ifndef EVLAMP_EEPROM_H_
#define EVLAMP_EEPROM_H_

#include "common.h"

/**
 * AT24C32..AT24C512 I2C EEPROM driver (16 bit memory addressing)
 */

/**
 * Probe and initialize EEPROM chip configured in menuconfig.
 * Returns ESP_ERR_NOT_FOUND if the chip does not respond.
 */
esp_err_t eeprom_init();

size_t eeprom_size();

size_t eeprom_page_size();

esp_err_t eeprom_read(size_t addr, void *data, size_t len);

/**
 * Write data, splitting it by page boundaries and waiting for each page
 * write cycle to complete
 */
esp_err_t eeprom_write(size_t addr, const void *data, size_t len);

#endif /* EVLAMP_EEPROM_H_ */
