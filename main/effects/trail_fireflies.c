/**
 * Fireflies with trails effect
 *
 * From GyverLamp (gunner47_v2): "Светлячки со шлейфом"
 * https://github.com/nikitakiselev/gunner47_v2
 *
 * A few dots fly straight and bounce off the edges, leaving fading trails.
 */
#include "effects/trail_fireflies.h"

#include <lib8tion.h>

#define MAX_FIREFLIES 10

#define P_COUNT   0
#define P_HUE     1
#define P_FADEOUT 2

EFFECT_PARAMS(trail_fireflies, 4) = {
    DECL_PARAM_RANGE(P_COUNT, "fireflies", "Number of fireflies", 1, MAX_FIREFLIES, 3),
    DECL_PARAM_RANGE(P_HUE, "hue", "Hue", 0, 255, 93),
    DECL_PARAM_RANGE(P_FADEOUT, "fadeout", "Fadeout speed", 10, 200, 70),
    DECL_PARAM_TEMPO(3, 30),
};

typedef struct
{
    int pos[2];   // tenths of a pixel
    int speed[2]; // tenths of a pixel per frame
} firefly_t;

static firefly_t flies[MAX_FIREFLIES];

esp_err_t effect_trail_fireflies_prepare(framebuffer_t *fb)
{
    for (size_t i = 0; i < MAX_FIREFLIES; i++)
    {
        flies[i].pos[0] = fb->width / 2 * 10;
        flies[i].pos[1] = fb->height / 2 * 10;
        for (size_t a = 0; a < 2; a++)
            flies[i].speed[a] = random8_between(4, 15) * (random8_to(2) ? 1 : -1);
    }
    return ESP_OK;
}

esp_err_t effect_trail_fireflies_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    fb_fade(fb, PARAM_VAL(trail_fireflies, P_FADEOUT));

    int max[2] = { ((int)fb->width - 1) * 10, ((int)fb->height - 1) * 10 };
    for (size_t i = 0; i < PARAM_VAL(trail_fireflies, P_COUNT); i++)
    {
        firefly_t *f = &flies[i];
        for (size_t a = 0; a < 2; a++)
        {
            f->pos[a] += f->speed[a];
            if (f->pos[a] < 0)
            {
                f->pos[a] = 0;
                f->speed[a] = -f->speed[a];
            }
            if (f->pos[a] > max[a])
            {
                f->pos[a] = max[a];
                f->speed[a] = -f->speed[a];
            }
        }
        fb_set_pixelf_hsv(fb, f->pos[0] / 10.0f, f->pos[1] / 10.0f,
                hsv_from_values(PARAM_VAL(trail_fireflies, P_HUE) * (i + 1), 255, 255));
    }

    return fb_end(fb);
}
