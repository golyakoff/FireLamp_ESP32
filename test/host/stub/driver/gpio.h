#pragma once
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
#define GPIO_MODE_INPUT 1
#define GPIO_INTR_DISABLE 0
static inline esp_err_t gpio_config(const gpio_config_t *c) { (void)c; return ESP_OK; }
extern int fake_level;
static inline int gpio_get_level(int g) { (void)g; return fake_level; }
