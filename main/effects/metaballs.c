/**
 * Metaballs effect
 *
 * Author: Yaroslaw Turbin 02.09.2020 (https://vk.com/ldirko, https://www.reddit.com/user/ldirko/)
 *
 * https://wokwi.com/arduino/libraries/FastLED/Metaballs
 */
#include "effects/metaballs.h"
#include "palettes.h"

#include <lib8tion.h>

#define MAX_BALLS 6
#define MAX_PALETTES 3

#define P_PALETTE 0
#define P_COUNT   1

EFFECT_PARAMS(metaballs, 3) = {
    DECL_PARAM_RANGE(P_PALETTE, "palette", "Palette", 0, MAX_PALETTES - 1, 0),
    DECL_PARAM_RANGE(P_COUNT, "balls", "Balls", 2, MAX_BALLS, 3),
    DECL_PARAM_TEMPO(2, 100),
};

enum {
    PAL_FIRE = 0,
    PAL_BLUE,
    PAL_GREEN,
};

#define PALETTE_SIZE 6

static const rgb_t _palettes[MAX_PALETTES][PALETTE_SIZE] = {
    [PAL_FIRE] =  { C_BLACK, C_BLACK, C_RED, C_YELLOW, C_YELLOW, C_WHITE },
    [PAL_BLUE] =  { C_BLACK, C_BLACK, C_DARK_BLUE, C_CYAN, C_CYAN, C_WHITE },
    [PAL_GREEN] = { C_BLACK, C_BLACK, C_DARK_GREEN, C_SPRING_GREEN, C_SPRING_GREEN, C_WHITE },
};

// the centers of the balls are in 1/256 of a pixel: whole pixels made them jump, most visibly
// at a slow tempo, where the steps are rare and the blended frames smear every jump
typedef struct {
    uint8_t bpm_x, bpm_y;
    uint8_t phase;
    int32_t x, y;
} ball_t;

ball_t balls[MAX_BALLS];

static uint32_t isqrt32(uint32_t v)
{
    uint32_t r = 0, bit = 1UL << 30;
    while (bit > v)
        bit >>= 2;
    while (bit)
    {
        if (v >= r + bit)
        {
            v -= r + bit;
            r = (r >> 1) + bit;
        }
        else
            r >>= 1;
        bit >>= 2;
    }
    return r;
}

// 255 / distance, 255 closer than a pixel; x, y and the ball in 1/256 of a pixel
static uint8_t dist(int32_t x, int32_t y, const ball_t *b)
{
    int32_t dx = b->x - x;
    int32_t dy = b->y - y;
    uint32_t d = isqrt32(dx * dx + dy * dy);
    return d > 256 ? 255 * 256 / d : 255;
}

esp_err_t effect_metaballs_prepare(framebuffer_t *fb)
{
    for (size_t i = 0; i < PARAM_VAL(metaballs, P_COUNT); i++)
    {
        balls[i].bpm_x = random8_between(10, 40);
        balls[i].bpm_y = random8_between(10, 40);
        balls[i].phase = 256 / MAX_BALLS * i;
    }

    return ESP_OK;
}

esp_err_t effect_metaballs_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    for (size_t i = 0; i < PARAM_VAL(metaballs, P_COUNT); i++)
    {
        balls[i].x = beatsin16(balls[i].bpm_x, 0, (fb->width - 1) * 256, 0, balls[i].phase << 8);
        balls[i].y = beatsin16(balls[i].bpm_y, 0, (fb->height - 1) * 256, 0, balls[i].phase << 8);
    }

    for (int i = 0; i < fb->width; i++)
    {
        for (int j = 0; j < fb->height; j++)
        {
            uint8_t sum = 0;
            for (size_t b = 0; b < PARAM_VAL(metaballs, P_COUNT); b++)
                sum = qadd8(sum, dist(i * 256, j * 256, &balls[b]));

            fb_set_pixel_rgb(fb, i, j, palette_color_rgb(_palettes[PARAM_VAL(metaballs, P_PALETTE)], PALETTE_SIZE, qsub8(sum, 32), 255, true));
        }
    }

    fb_blur2d(fb, 32);

    return fb_end(fb);
}
