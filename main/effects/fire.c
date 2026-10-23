/**
 * @file fire.c
 *
 * Turbin fire: fire based on Perlin noise with sparks
 *
 * Author: Yaroslaw Turbin (https://vk.com/ldirko, https://www.reddit.com/user/ldirko/)
 * https://pastebin.com/jSSVSRi6
 *
 * Sparks from "Огонь 2020" (c) SottNick, GyverLamp gunner47_v2:
 * https://github.com/nikitakiselev/gunner47_v2
 */
#include "effects/fire.h"

#include <lib8tion.h>
#include <noise.h>
#include "palettes.h"

#define MAX_SCALE 100
#define HALF_SCALE (MAX_SCALE / 2)


// Sparks rise from the bottom SPARK_ROWS rows of the flame, are seen above them with the colour of
// the flame there, and move aside once in about SPARK_DRIFT steps
#define SPARK_ROWS  4
#define SPARK_STEP  128 // rise per effect step, 1/256 of a row: 30 rows per second
#define SPARK_DRIFT 16
#define SPARK_FADE  2   // per row above the flame
#define MAX_SPARKS  8   // one per 8 columns

#define P_PALETTE 0
#define P_SCALE   1
#define P_SPARKS  2

EFFECT_PARAMS(fire, 4) = {
    DECL_PARAM_RANGE(P_PALETTE, "palette", "Palette", PALETTE_FIRE, PALETTE_LITHIUM_FIRE, 0),
    DECL_PARAM_RANGE(P_SCALE, "scale", "Scale", 10, MAX_SCALE, 60),
    DECL_PARAM_BOOL(P_SPARKS, "sparks", "Sparks", 1),
    DECL_PARAM_TEMPO(3, 100),
};

typedef struct
{
    uint8_t x;
    uint16_t y; // rows from the bottom, x256
} spark_t;

static uint32_t time_scale = 1;
static spark_t sparks[MAX_SPARKS];
static size_t spark_count = 0;

static void spark_reset(framebuffer_t *fb, spark_t *s)
{
    s->x = random8_to(fb->width);
    s->y = random8_to(SPARK_ROWS) << 8;
}

static void draw_sparks(framebuffer_t *fb)
{
    size_t from = fb->height > SPARK_ROWS ? SPARK_ROWS - 1 : 0;
    for (size_t i = 0; i < spark_count; i++)
    {
        spark_t *s = &sparks[i];
        size_t y = s->y >> 8;
        if (y > from && y < fb->height)
        {
            rgb_t c;
            fb_get_pixel_rgb(fb, s->x, from, &c);
            uint8_t scale = 255 - y * SPARK_FADE;
            c.r = scale8(c.r, scale);
            c.g = scale8(c.g, scale);
            c.b = scale8(c.b, scale);
            fb_set_pixel_rgb(fb, s->x, y, c);
        }

        s->y += SPARK_STEP;
        if ((s->y >> 8) >= fb->height)
            spark_reset(fb, s);
        if (!random8_to(SPARK_DRIFT))
            s->x = (s->x + fb->width + 1 - random8_to(3)) % fb->width;
    }
}

esp_err_t effect_fire_prepare(framebuffer_t *fb)
{
    time_scale = PARAM_VAL(fire, P_SCALE) >= HALF_SCALE ? 1 : (HALF_SCALE - PARAM_VAL(fire, P_SCALE)) / 2;

    spark_count = MAX(1, MIN(MAX_SPARKS, fb->width / 8));
    for (size_t i = 0; i < spark_count; i++)
    {
        spark_reset(fb, &sparks[i]);
        sparks[i].y = random8_to(fb->height) << 8;
    }

    return ESP_OK;
}

esp_err_t effect_fire_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    uint32_t time_ms = esp_timer_get_time() / 1000;

    for (size_t x = 0; x < fb->width; x++)
        for (size_t y = 0; y < fb->height; y++)
        {
            uint8_t idx = qsub8(
                inoise8_3d(x * PARAM_VAL(fire, P_SCALE), y * PARAM_VAL(fire, P_SCALE) + time_ms / time_scale, time_ms / 3),
                abs8(y - (fb->height - 1)) * 255 / (fb->height - 1)
            );
            rgb_t c = palette_color_rgb(palettes[PARAM_VAL(fire, P_PALETTE)].palette, palettes[PARAM_VAL(fire, P_PALETTE)].size, idx, 255, true);
            fb_set_pixel_rgb(fb, x, fb->height - y - 1, c);
        }

    if (PARAM_VAL(fire, P_SPARKS))
        draw_sparks(fb);

    return fb_end(fb);
}
