/**
 * Snowfall effect
 *
 * From GyverLamp (gunner47_v2): "Снегопад"
 * https://github.com/nikitakiselev/gunner47_v2
 *
 * Snowflakes of a few shades of white appear in the top line and fall straight down.
 */
#include "effects/snow.h"

#include <lib8tion.h>

#define P_DENSITY 0

EFFECT_PARAMS(snow, 2) = {
    DECL_PARAM_RANGE(P_DENSITY, "density", "Density", 0, 99, 90),
    DECL_PARAM_TEMPO(1, 22),
};

esp_err_t effect_snow_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    size_t top = fb->height - 1;

    // move down
    for (size_t y = 0; y < top; y++)
        for (size_t x = 0; x < fb->width; x++)
        {
            rgb_t c;
            fb_get_pixel_rgb(fb, x, y + 1, &c);
            fb_set_pixel_rgb(fb, x, y, c);
        }

    // new flakes, never two in a column next to each other
    for (size_t x = 0; x < fb->width; x++)
    {
        rgb_t below = { 0 }, c = { 0 };
        if (top)
            fb_get_pixel_rgb(fb, x, top - 1, &below);
        if (rgb_is_zero(below) && !random8_to(100 - PARAM_VAL(snow, P_DENSITY)))
        {
            uint8_t shade = random8_to(4) * 0x10;
            c = rgb_from_values(0xe0 - shade, 0xff - shade, 0xff - shade);
        }
        fb_set_pixel_rgb(fb, x, top, c);
    }

    return fb_end(fb);
}
