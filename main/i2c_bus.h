#ifndef EVLAMP_I2C_BUS_H_
#define EVLAMP_I2C_BUS_H_

#include "common.h"
#include <driver/i2c_master.h>

/**
 * Initialize shared I2C master bus (GPIOs from menuconfig).
 * Safe to call several times, the bus is created once.
 */
esp_err_t i2c_bus_init();

/**
 * Get shared I2C bus handle, NULL if the bus is not initialized
 */
i2c_master_bus_handle_t i2c_bus_get();

#endif /* EVLAMP_I2C_BUS_H_ */
