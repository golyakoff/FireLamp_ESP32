/**
 * Starfall / snowstorm effect
 *
 * Author: PalPalych, from GyverLamp (gunner47_v2)
 * https://github.com/nikitakiselev/gunner47_v2
 *
 * Comet heads appear in the top line and fade there, the picture moves down and to the right,
 * so the faded copies of a head become its tail.
 */
#include "effects/starfall.h"

#include <lib8tion.h>

#define P_COLORED    0
#define P_SATURATION 1
#define P_DENSITY    2
#define P_TAIL       3

EFFECT_PARAMS(starfall, 4) = {
    DECL_PARAM_BOOL(P_COLORED, "colored", "Colored", 1),
    DECL_PARAM_RANGE(P_SATURATION, "saturation", "Saturation", 0, 255, 140),
    DECL_PARAM_RANGE(P_DENSITY, "density", "Density", 1, 100, 50),
    DECL_PARAM_RANGE(P_TAIL, "tail_length", "Tail length", 0, 100, 50),
};

static bool is_off(framebuffer_t *fb, size_t x, size_t y)
{
    rgb_t c;
    fb_get_pixel_rgb(fb, x, y, &c);
    return rgb_is_zero(c);
}

esp_err_t effect_starfall_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    size_t top = fb->height - 1;

    // new heads, never next to each other
    for (size_t x = 0; x < fb->width; x++)
    {
        if (random16_to(1600) < PARAM_VAL(starfall, P_DENSITY)
                && is_off(fb, x, top)
                && is_off(fb, (x + 1) % fb->width, top)
                && is_off(fb, (x + fb->width - 1) % fb->width, top))
            fb_set_pixel_hsv(fb, x, top, hsv_from_values(random8(),
                    PARAM_VAL(starfall, P_COLORED) ? PARAM_VAL(starfall, P_SATURATION) : 0,
                    random8_between(64, 255)));
    }

    // move down and to the right
    for (size_t y = 0; y < top; y++)
        for (size_t x = 0; x < fb->width; x++)
        {
            rgb_t c;
            fb_get_pixel_rgb(fb, x, y + 1, &c);
            fb_set_pixel_rgb(fb, (x + 1) % fb->width, y, c);
        }

    // fade the top line: the tails
    uint8_t step = 255 - PARAM_VAL(starfall, P_TAIL) * 5 / 2;
    for (size_t x = 0; x < fb->width; x++)
    {
        rgb_t c;
        fb_get_pixel_rgb(fb, x, top, &c);
        if (rgb_is_zero(c))
            continue;
        if (c.r < 30 && c.g < 30 && c.b < 30)
            c = rgb_from_values(0, 0, 0);
        else
            c = rgb_fade(c, step);
        fb_set_pixel_rgb(fb, x, top, c);
    }

    return fb_end(fb);
}
