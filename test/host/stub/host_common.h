// Host stubs for ESP-IDF / FreeRTOS, enough to compile storage.c and input.c.
// Force-included (-include) instead of main/common.h: defines its include guard.
#pragma once
#define EVLAMP_COMMON_H_
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1110
#define BIT64(n) (1ULL << (n))
#define TAG "test"
#define ESP_LOGI(t, ...) do {} while (0)
#define ESP_LOGD(t, ...) do {} while (0)
#define ESP_LOGW(t, ...) do {} while (0)
#define ESP_LOGE(t, ...) do {} while (0)
static inline const char *esp_err_to_name(esp_err_t e) { (void)e; return "err"; }
#define CHECK(x) do { esp_err_t __; if ((__ = (x)) != ESP_OK) return __; } while (0)
#define CHECK_ARG(v) do { if (!(v)) return ESP_ERR_INVALID_ARG; } while (0)
#define CHECK_LOGE(x, msg, ...) CHECK(x)
// FreeRTOS
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef int portMUX_TYPE;
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) (void)(m)
#define portEXIT_CRITICAL(m) (void)(m)
#define portMAX_DELAY 0xffffffff
#define pdPASS 1
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
#define xSemaphoreTake(s, t) (void)0
#define xSemaphoreGive(s) (void)0
static inline uint32_t ulTaskNotifyTake(int c, uint32_t t) { (void)c; (void)t; return 0; }
#define xTaskNotifyGive(t) (void)0
static inline BaseType_t xTaskCreate(void *f, const char *n, int s, void *a, int p, TaskHandle_t *h) { (void)f;(void)n;(void)s;(void)a;(void)p; if (h) *h = (void *)1; return pdPASS; }
#define xTaskGetTickCount() 0
#define vTaskDelayUntil(a, b) (void)0
#define vTaskDelay(x) (void)0
// project config: keep in sync with main/config.h
#define SETTINGS_MAGIC 0xf1a30001
#define STORAGE_SAVE_DELAY_MS 15000
#define STORAGE_TASK_STACK_SIZE 4096
#define STORAGE_TASK_PRIORITY 2
#define INPUT_TASK_STACK_SIZE 3072
#define INPUT_TASK_PRIORITY 6
#define INPUT_POLL_MS 10
#define INPUT_DEBOUNCE_SAMPLES 3
#define INPUT_CLICK_TIMEOUT_MS 350
#define INPUT_MAX_CLICKS 5
#define INPUT_HOLD_MS 600
#define INPUT_HOLD_REPEAT_MS 50
#define BUS_EVENT_DATA_SIZE 64
