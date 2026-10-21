#pragma once
static inline esp_err_t esp_register_shutdown_handler(void (*h)(void)) { (void)h; return ESP_OK; }
