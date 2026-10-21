#include "ds3231.h"

#if CONFIG_FL_RTC_DS3231
#include <string.h>
#include "i2c_bus.h"

#define DS3231_ADDR 0x68

#define REG_TIME    0x00
#define REG_CONTROL 0x0e
#define REG_STATUS  0x0f

#define STATUS_OSF  BIT(7)

#define I2C_TIMEOUT_MS 50

static i2c_master_dev_handle_t dev = NULL;

static inline uint8_t bcd2dec(uint8_t v)
{
    return (v >> 4) * 10 + (v & 0x0f);
}

static inline uint8_t dec2bcd(uint8_t v)
{
    return ((v / 10) << 4) + v % 10;
}

static esp_err_t read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, I2C_TIMEOUT_MS);
}

static esp_err_t write_regs(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[8];
    CHECK_ARG(len < sizeof(buf));
    buf[0] = reg;
    memcpy(buf + 1, data, len);
    return i2c_master_transmit(dev, buf, len + 1, I2C_TIMEOUT_MS);
}

esp_err_t ds3231_init()
{
    if (dev)
        return ESP_OK;

    CHECK(i2c_bus_init());

    if (i2c_master_probe(i2c_bus_get(), DS3231_ADDR, I2C_TIMEOUT_MS) != ESP_OK)
    {
        ESP_LOGW(TAG, "DS3231 not found");
        return ESP_ERR_NOT_FOUND;
    }

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_ADDR,
        .scl_speed_hz = CONFIG_FL_I2C_FREQ_HZ,
    };
    CHECK_LOGE(i2c_master_bus_add_device(i2c_bus_get(), &cfg, &dev), "Could not add DS3231 device");

    ESP_LOGI(TAG, "DS3231 found");

    return ESP_OK;
}

esp_err_t ds3231_get_time(time_t *utc)
{
    CHECK_ARG(dev && utc);

    uint8_t status;
    CHECK(read_regs(REG_STATUS, &status, 1));

    uint8_t r[7];
    CHECK(read_regs(REG_TIME, r, sizeof(r)));

    struct tm t = {
        .tm_sec  = bcd2dec(r[0] & 0x7f),
        .tm_min  = bcd2dec(r[1] & 0x7f),
        .tm_hour = r[2] & 0x40
            // 12 hour mode, bit 5 is PM
            ? bcd2dec(r[2] & 0x1f) % 12 + (r[2] & 0x20 ? 12 : 0)
            : bcd2dec(r[2] & 0x3f),
        .tm_mday = bcd2dec(r[4] & 0x3f),
        .tm_mon  = bcd2dec(r[5] & 0x1f) - 1,
        .tm_year = bcd2dec(r[6]) + 100, // 2000..2099
    };

    // timegm() equivalent, without depending on the TZ environment variable
    int y = t.tm_year + 1900, m = t.tm_mon + 1;
    if (m <= 2) { y--; m += 12; }
    int64_t days = 365LL * y + y / 4 - y / 100 + y / 400 + (153 * (m - 3) + 2) / 5 + t.tm_mday - 719469;
    *utc = (time_t)(days * 86400 + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec);

    return status & STATUS_OSF ? ESP_ERR_INVALID_STATE : ESP_OK;
}

esp_err_t ds3231_set_time(time_t utc)
{
    CHECK_ARG(dev);

    struct tm t;
    gmtime_r(&utc, &t);

    uint8_t r[7] = {
        dec2bcd(t.tm_sec),
        dec2bcd(t.tm_min),
        dec2bcd(t.tm_hour), // 24 hour mode
        t.tm_wday + 1,
        dec2bcd(t.tm_mday),
        dec2bcd(t.tm_mon + 1),
        dec2bcd(t.tm_year % 100),
    };
    CHECK(write_regs(REG_TIME, r, sizeof(r)));

    uint8_t status;
    CHECK(read_regs(REG_STATUS, &status, 1));
    status &= ~STATUS_OSF;
    CHECK(write_regs(REG_STATUS, &status, 1));

    return ESP_OK;
}

#endif /* CONFIG_FL_RTC_DS3231 */
