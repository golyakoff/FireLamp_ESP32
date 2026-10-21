#pragma once
// In-memory NVS for host tests, implemented in the test
typedef int nvs_handle_t;
enum { NVS_READONLY, NVS_READWRITE };
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h);
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *data, size_t *len);
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t len);
esp_err_t nvs_commit(nvs_handle_t h);
void nvs_close(nvs_handle_t h);
