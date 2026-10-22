// Host stub for tools/preview: just enough of the firmware headers to run the effects on a PC
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "framebuffer.h"
#define CHECK(x) do { esp_err_t __ = (x); if (__) return __; } while (0)
#define CHECK_ARG(v) do { if (!(v)) return ESP_ERR_INVALID_ARG; } while (0)
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
extern const char *TAG;
#define ESP_LOGE(t, f, ...) fprintf(stderr, "E " f "\n", ##__VA_ARGS__)
#define ESP_LOGW(t, f, ...) fprintf(stderr, "W " f "\n", ##__VA_ARGS__)
#define ESP_LOGI(t, f, ...) do {} while (0)
#define ESP_LOGD(t, f, ...) do {} while (0)
typedef enum { PARAM_TYPE_RANGE = 0, PARAM_TYPE_BOOL } effect_param_type_t;
typedef struct { const char *code; char name[32]; uint8_t min, max, def, value, type; } effect_param_t;
#define EFFECT_PARAMS(name, count) effect_param_t effect_ ##name## _params[count]
#define DECL_PARAM_RANGE(ID, CODE, NAME, MIN_, MAX_, DEF) [ID] = { .code = CODE, .name = NAME, .min = MIN_, .max = MAX_, .def = DEF, .value = DEF, .type = PARAM_TYPE_RANGE }
#define DECL_PARAM_BOOL(ID, CODE, NAME, DEF) [ID] = { .code = CODE, .name = NAME, .min = 0, .max = 1, .def = DEF, .value = DEF, .type = PARAM_TYPE_BOOL }
#define PARAM_VAL(name, X) (effect_ ##name## _params[(X)].value)
// the previews ignore the tempo: no frame blending on a PC
#define EFFECT_TEMPO_MIN 10
#define EFFECT_TEMPO_MAX 100
#define DECL_PARAM_TEMPO(ID, DEF) DECL_PARAM_RANGE(ID, "tempo", "Tempo", EFFECT_TEMPO_MIN, EFFECT_TEMPO_MAX, DEF)
typedef esp_err_t (*effect_prepare_cb)(framebuffer_t *fb);
typedef esp_err_t (*effect_run_cb)(framebuffer_t *fb);
typedef struct { const char *code; char name[32]; effect_prepare_cb prepare; effect_run_cb run; effect_param_t *params; size_t params_count; } effect_descriptor_t;
#define DECL_EFFECT_FULL(CODE, NAME) { .code = #CODE, .name = NAME, .prepare = effect_ ##CODE## _prepare, .run = effect_ ##CODE## _run, .params = effect_ ##CODE## _params, .params_count = sizeof(effect_ ##CODE## _params) / sizeof(effect_param_t) }
#define DECL_EFFECT_SHORT(CODE, NAME) { .code = #CODE, .name = NAME, .prepare = NULL, .run = effect_ ##CODE## _run, .params = effect_ ##CODE## _params, .params_count = sizeof(effect_ ##CODE## _params) / sizeof(effect_param_t) }
