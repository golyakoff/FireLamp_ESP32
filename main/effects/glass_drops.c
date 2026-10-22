/**
 * Drops on glass effect
 *
 * From GyverLamp (gunner47_v2): "Капли на стекле", originally from FireLamp_JeeUI
 * https://github.com/nikitakiselev/gunner47_v2
 * https://github.com/DmytroKorniienko/FireLamp_JeeUI
 *
 * Drops run down with long fading trails and sometimes jump aside.
 */
#include "effects/glass_drops.h"

#include <lib8tion.h>

#define MAX_DROPS 100
#define SPEED     0.136f // pixels per frame for the speed factor 1
#define JUMP      20     // chance of a jump aside, of 256
#define TRAIL     246    // brightness kept per frame, of 256

#define MODE_COLOR   0
#define MODE_RAINBOW 1
#define MODE_GRAY    2

#define P_COUNT 0
#define P_MODE  1
#define P_HUE   2

EFFECT_PARAMS(glass_drops, 4) = {
    DECL_PARAM_RANGE(P_COUNT, "drops", "Number of drops", 1, MAX_DROPS, 16),
    DECL_PARAM_RANGE(P_MODE, "mode", "Mode", MODE_COLOR, MODE_GRAY, MODE_COLOR),
    DECL_PARAM_RANGE(P_HUE, "hue", "Hue", 0, 255, 150),
    DECL_PARAM_TEMPO(3, 50),
};

typedef struct
{
    float x, y;
    float speed;
    uint8_t value;
} drop_t;

static drop_t drops[MAX_DROPS];
static uint8_t hue;

static void spawn(framebuffer_t *fb, drop_t *d, size_t min_y)
{
    d->x = random8_to(fb->width);
    d->y = random8_between(min_y, fb->height);
    d->speed = random8_between(150, 250) / 100.0f;
    d->value = random8_between(127, 255);
}

esp_err_t effect_glass_drops_prepare(framebuffer_t *fb)
{
    for (size_t i = 0; i < MAX_DROPS; i++)
        spawn(fb, &drops[i], 0);
    hue = PARAM_VAL(glass_drops, P_HUE);
    return ESP_OK;
}

esp_err_t effect_glass_drops_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    fb_fade(fb, 255 - TRAIL);

    // the number of drops is given for a 16x16 matrix
    size_t count = PARAM_VAL(glass_drops, P_COUNT) * fb->width * fb->height / 256;
    count = count < 1 ? 1 : (count > MAX_DROPS ? MAX_DROPS : count);

    uint8_t mode = PARAM_VAL(glass_drops, P_MODE);
    if (mode != MODE_RAINBOW)
        hue = PARAM_VAL(glass_drops, P_HUE);

    for (size_t i = 0; i < count; i++)
    {
        drop_t *d = &drops[i];
        d->y -= d->speed * SPEED;

        hsv_t c;
        if (mode == MODE_GRAY)
            c = hsv_from_values(0, 0, d->value);
        else
            c = hsv_from_values(mode == MODE_RAINBOW ? ++hue : hue, 255, d->value);
        if (d->y >= 0)
            fb_set_pixelf_hsv(fb, d->x, d->y, c);

        if (random8() < JUMP)
        {
            d->x = (size_t)(d->x + fb->width - 1 + random8_to(3)) % fb->width;
            d->value = random8_between(196, 255);
        }

        if (d->y < -1)
            spawn(fb, d, fb->height - fb->height / 2);
    }

    return fb_end(fb);
}
