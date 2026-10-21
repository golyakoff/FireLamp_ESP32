#include "eeprom.h"

#include <string.h>
#include <esp_timer.h>
#include "i2c_bus.h"

// Used the same way for any AT24C32..AT24C512: the settings take less than 4 KB, and 32 byte
// page writes are valid for all of them (bigger chips just have bigger pages)
#define EEPROM_SIZE (4 * 1024)
#define EEPROM_PAGE 32

#define I2C_TIMEOUT_MS     50
#define WRITE_CYCLE_MAX_US 20000 // datasheet: 5..10 ms

static i2c_master_dev_handle_t dev = NULL;

// The chip does not ACK its address while an internal write cycle is in progress
static esp_err_t wait_ready()
{
    int64_t start = esp_timer_get_time();
    while (i2c_master_probe(i2c_bus_get(), CONFIG_FL_EEPROM_ADDR, I2C_TIMEOUT_MS) != ESP_OK)
    {
        if (esp_timer_get_time() - start > WRITE_CYCLE_MAX_US)
            return ESP_ERR_TIMEOUT;
        vTaskDelay(1);
    }
    return ESP_OK;
}

esp_err_t eeprom_init()
{
    if (dev)
        return ESP_OK;

    CHECK(i2c_bus_init());

    if (i2c_master_probe(i2c_bus_get(), CONFIG_FL_EEPROM_ADDR, I2C_TIMEOUT_MS) != ESP_OK)
    {
        ESP_LOGI(TAG, "No EEPROM at address 0x%02x", CONFIG_FL_EEPROM_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_FL_EEPROM_ADDR,
        .scl_speed_hz = CONFIG_FL_I2C_FREQ_HZ,
    };
    CHECK_LOGE(i2c_master_bus_add_device(i2c_bus_get(), &cfg, &dev),
            "Could not add EEPROM device");

    ESP_LOGI(TAG, "EEPROM found at 0x%02x", CONFIG_FL_EEPROM_ADDR);

    return ESP_OK;
}

size_t eeprom_size()
{
    return EEPROM_SIZE;
}

size_t eeprom_page_size()
{
    return EEPROM_PAGE;
}

esp_err_t eeprom_read(size_t addr, void *data, size_t len)
{
    CHECK_ARG(dev && data && addr + len <= EEPROM_SIZE);

    uint8_t a[2] = { addr >> 8, addr & 0xff };
    return i2c_master_transmit_receive(dev, a, sizeof(a), data, len, I2C_TIMEOUT_MS);
}

esp_err_t eeprom_write(size_t addr, const void *data, size_t len)
{
    CHECK_ARG(dev && data && addr + len <= EEPROM_SIZE);

    const uint8_t *src = data;
    uint8_t buf[2 + EEPROM_PAGE];

    while (len)
    {
        // page write wraps around within the page, so never cross its boundary
        size_t chunk = EEPROM_PAGE - addr % EEPROM_PAGE;
        if (chunk > len)
            chunk = len;

        buf[0] = addr >> 8;
        buf[1] = addr & 0xff;
        memcpy(buf + 2, src, chunk);
        CHECK(i2c_master_transmit(dev, buf, chunk + 2, I2C_TIMEOUT_MS));
        CHECK(wait_ready());

        addr += chunk;
        src += chunk;
        len -= chunk;
    }

    return ESP_OK;
}
