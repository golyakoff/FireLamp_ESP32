#ifndef EVLAMP_EFFECT_H_
#define EVLAMP_EFFECT_H_

#include "common.h"
#include <framebuffer.h>

typedef enum {
    PARAM_TYPE_RANGE = 0,
    PARAM_TYPE_BOOL,
} effect_param_type_t;

#define EFFECT_PARAMS(name, count) effect_param_t effect_ ##name## _params[count]

// CODE: stable identifier used by the app, API and settings storage: up to EFFECT_CODE_MAX_LEN
// characters a-z, 0-9, '_'. Never change codes of existing effects and parameters.
#define DECL_PARAM_RANGE(ID, CODE, NAME, MIN, MAX, DEF) [ID] = { .code = CODE, .name = NAME, .min = MIN, .max = MAX, .def = DEF, .value = DEF, .type = PARAM_TYPE_RANGE }
#define DECL_PARAM_BOOL(ID, CODE, NAME, DEF) [ID] = { .code = CODE, .name = NAME, .min = 0, .max = 1, .def = DEF, .value = DEF, .type = PARAM_TYPE_BOOL }

#define PARAM_VAL(name, X) (effect_ ##name## _params[(X)].value)

// Tempo, percent: effect steps per output frame. Below 100 the frames between the steps are
// blended (surface.c), so a slower effect stays smooth at the same FPS. Only for the effects
// without their own speed parameter; the others always run at EFFECT_TEMPO_MAX.
#define EFFECT_TEMPO_MIN 10
#define EFFECT_TEMPO_MAX 100
#define DECL_PARAM_TEMPO(ID, DEF) DECL_PARAM_RANGE(ID, "tempo", "Tempo", EFFECT_TEMPO_MIN, EFFECT_TEMPO_MAX, DEF)

// Effect code is the C identifier of the effect, e.g. `fire`, `plasma_waves`
#define DECL_EFFECT_FULL(CODE, NAME) { \
        .code = #CODE, \
        .name = NAME, \
        .prepare = effect_ ##CODE## _prepare, \
        .run = effect_ ##CODE## _run, \
        .params = effect_ ##CODE## _params, \
        .params_count = sizeof(effect_ ##CODE## _params) / sizeof(effect_param_t) }

#define DECL_EFFECT_SHORT(CODE, NAME) { \
        .code = #CODE, \
        .name = NAME, \
        .prepare = NULL, \
        .run = effect_ ##CODE## _run, \
        .params = effect_ ##CODE## _params, \
        .params_count = sizeof(effect_ ##CODE## _params) / sizeof(effect_param_t) }

#define EFFECT_CODE_MAX_LEN 16

typedef struct
{
    const char *code;
    char name[32];
    uint8_t min, max, def, value, type;
} effect_param_t;

typedef esp_err_t (*effect_prepare_cb)(framebuffer_t *fb);
typedef esp_err_t (*effect_run_cb)(framebuffer_t *fb);

typedef struct
{
    const char *code;
    char name[32];
    effect_prepare_cb prepare;
    effect_run_cb run;
    effect_param_t *params;
    size_t params_count;
} effect_descriptor_t;

////////////////////////////////////////////////////////////////////////////////

extern const effect_descriptor_t effects[];

extern const size_t effects_count;

esp_err_t effects_reset();

esp_err_t effect_param_set(size_t effect, size_t param, uint8_t value);

esp_err_t effects_init();

/**
 * Find effect by code
 *
 * @return Effect number or -1 if not found
 */
int effect_find(const char *code);

/**
 * Find parameter of the effect by code
 *
 * @return Parameter number or -1 if not found
 */
int effect_param_find(size_t effect, const char *code);

/**
 * Check that a string is a valid code: 1..EFFECT_CODE_MAX_LEN characters a-z, 0-9, '_'
 */
bool effect_code_valid(const char *code);

/**
 * Tempo of the effect: its `tempo` parameter, EFFECT_TEMPO_MAX for the effects without it
 */
uint8_t effect_tempo(size_t effect);

/**
 * Brightest frame of the effect, percent of the full white matrix (main/effect_peaks.h).
 * The brightness of the effect is capped by it, see surface.c
 */
uint8_t effect_peak(size_t effect);

typedef void (*effect_param_cb_t)(size_t effect, size_t param);

/**
 * Callback called when a parameter value changes
 */
void effect_set_param_cb(effect_param_cb_t cb);

#endif /* EVLAMP_EFFECT_H_ */
