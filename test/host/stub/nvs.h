#pragma once
typedef int nvs_handle_t;
enum { NVS_READONLY, NVS_READWRITE };
static inline esp_err_t nvs_open(const char *n, int m, nvs_handle_t *h) { (void)n;(void)m;(void)h; return ESP_ERR_NVS_NOT_FOUND; }
static inline esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *d, size_t *l) { return ESP_FAIL; }
static inline esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *d, size_t l) { return ESP_FAIL; }
static inline esp_err_t nvs_commit(nvs_handle_t h) { return ESP_FAIL; }
static inline void nvs_close(nvs_handle_t h) { }
