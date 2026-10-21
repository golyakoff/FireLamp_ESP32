#include "i2c_bus.h"


#ifdef CONFIG_FL_I2C_INTERNAL_PULLUP
    #define I2C_INTERNAL_PULLUP true
#else
    #define I2C_INTERNAL_PULLUP false
#endif

static i2c_master_bus_handle_t bus = NULL;

esp_err_t i2c_bus_init()
{
    if (bus)
        return ESP_OK;

    i2c_master_bus_config_t cfg = {
        .i2c_port = -1, // auto select
        .sda_io_num = CONFIG_FL_I2C_SDA_GPIO,
        .scl_io_num = CONFIG_FL_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = I2C_INTERNAL_PULLUP,
    };
    CHECK_LOGE(i2c_new_master_bus(&cfg, &bus),
            "Could not create I2C bus (SDA %d, SCL %d)", CONFIG_FL_I2C_SDA_GPIO, CONFIG_FL_I2C_SCL_GPIO);

    ESP_LOGI(TAG, "I2C bus ready: SDA %d, SCL %d", CONFIG_FL_I2C_SDA_GPIO, CONFIG_FL_I2C_SCL_GPIO);

    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get()
{
    return bus;
}
