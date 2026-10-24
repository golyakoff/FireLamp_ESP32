/**
 * Magma effect
 *
 * Author: SottNick, from GyverLamp (gunner47_v2): "Магма"
 * https://github.com/nikitakiselev/gunner47_v2
 *
 * A glowing noise layer, bright at the bottom and dark at the top, with drops of magma that leap
 * up, fall back and leap again.
 */
#include "effects/magma.h"

#include <math.h>
#include <lib8tion.h>
#include <noise.h>
#include "palettes.h"

#define MAX_DROPS   64
#define GRAVITY     0.06f
#define SETTLED     0.1f
#define FRICTION    0.95f // bounce off a wall
#define WIND        0.95f // air resistance
#define NOISE_X     12    // noise scale across
#define NOISE_Y     10    // noise scale up
#define DROP_COLOR  50    // palette index of the drops

#define P_PALETTE 0
#define P_DROPS   1

EFFECT_PARAMS(magma, 3) = {
    DECL_PARAM_RANGE(P_PALETTE, "palette", "Palette", PALETTE_FIRE, PALETTE_LITHIUM_FIRE, PALETTE_FIRE),
    DECL_PARAM_RANGE(P_DROPS, "drops", "Number of drops", 1, MAX_DROPS, 25),
    DECL_PARAM_TEMPO(2, 30),
};

typedef struct
{
    float x, y;
    float speed_x, speed_y;
} drop_t;

static drop_t drops[MAX_DROPS];
static uint16_t noise_y, noise_z;

// leap up and to the side, towards the center
static void leap(framebuffer_t *fb, drop_t *d)
{
    d->speed_x = random8_between(1, 100) / 100.0f;
    d->speed_y = 2 * random8_between(1, 100) / 100.0f;
    // for variety, sometimes 50 % faster
    if (random8() < 12)
    {
        d->speed_x *= 1.5f;
        d->speed_y *= 1.5f;
    }
    if (d->x > fb->width / 2)
        d->speed_x = -d->speed_x;
}

static void move(framebuffer_t *fb, drop_t *d)
{
    d->x += d->speed_x;
    d->y += d->speed_y;

    // the floor and the ceiling
    if (d->y < 0 || d->y > fb->height - 1)
    {
        d->speed_y = -d->speed_y * FRICTION;
        d->speed_x *= FRICTION;
        d->y += d->speed_y;
        if (d->y < 0)
            d->y = 0;
        if (d->y <= SETTLED && fabsf(d->speed_y) <= SETTLED)
            leap(fb, d);
    }

    // the sides
    if (d->x <= 0 || d->x >= fb->width - 1)
    {
        d->speed_x = -d->speed_x * FRICTION;
        d->x = d->x <= 0 ? -d->x : 2.0f * (fb->width - 1) - d->x;
    }

    d->speed_y -= GRAVITY;
    d->speed_x *= WIND;
    d->speed_y *= WIND;
}

esp_err_t effect_magma_prepare(framebuffer_t *fb)
{
    for (size_t i = 0; i < MAX_DROPS; i++)
    {
        drops[i].x = random8_to(fb->width);
        drops[i].y = random8_to(fb->height);
        leap(fb, &drops[i]);
    }
    return ESP_OK;
}

esp_err_t effect_magma_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    const palette_def_t *p = &palettes[PARAM_VAL(magma, P_PALETTE)];

    fb_fade(fb, 74);

    for (size_t x = 0; x < fb->width; x++)
        for (size_t j = 0; j < fb->height; j++)
        {
            // j counts from the top: darker up there
            uint8_t fade = fb->height > 1 ? (fb->height - 1 - j) * 255 / (fb->height - 1) : 0;
            uint8_t n = inoise8_3d(x * NOISE_X, (j + noise_y + random8_to(2)) * NOISE_Y, noise_z);
            fb_set_pixelf_rgb(fb, x, fb->height - 1 - j, palette_color_rgb(p->palette, p->size, qsub8(n, fade), 255, true));
        }

    rgb_t color = palette_color_rgb(p->palette, p->size, DROP_COLOR, 255, true);
    for (size_t i = 0; i < PARAM_VAL(magma, P_DROPS); i++)
    {
        move(fb, &drops[i]);
        if (drops[i].y >= fb->height / 4)
            fb_set_pixelf_rgb(fb, drops[i].x, drops[i].y, color);
    }

    noise_y++;
    if (noise_y & 1)
        noise_z++;

    return fb_end(fb);
}
