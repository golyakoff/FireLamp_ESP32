/**
 * Prismata effect
 *
 * Prismata Loading Animation, https://github.com/pixelmatix/aurora (PatternPendulumWave),
 * adapted by SottNick, from GyverLamp (gunner47_v2): "Призмата"
 * https://github.com/nikitakiselev/gunner47_v2
 *
 * Every column has a dot swinging up and down, each a little faster than the one before:
 * blurred trails of the dots weave waves of the palette colors.
 */
#include "effects/prismata.h"

#include <lib8tion.h>
#include "palettes.h"

#define P_PALETTE 0
#define P_FADEOUT 1
#define P_SPEED   2

EFFECT_PARAMS(prismata, 3) = {
    DECL_PARAM_RANGE(P_PALETTE, "palette", "Palette", 0, PALETTE_MAX - 1, PALETTE_PARTY),
    DECL_PARAM_RANGE(P_FADEOUT, "fadeout", "Fadeout", 0, 30, 3),
    DECL_PARAM_RANGE(P_SPEED, "speed", "Tempo", 1, 255, 100),
};

static uint8_t hue;

esp_err_t effect_prismata_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    const palette_def_t *p = &palettes[PARAM_VAL(prismata, P_PALETTE)];

    hue++;
    fb_blur2d(fb, 20);
    fb_fade(fb, PARAM_VAL(prismata, P_FADEOUT));

    // beatsin8() with a speed control: the original speed is at 20
    uint32_t ms = esp_timer_get_time() / 1000;
    for (size_t x = 0; x < fb->width; x++)
    {
        uint8_t beat = (uint32_t)(ms * (x + 1) * 28 * PARAM_VAL(prismata, P_SPEED)) >> 17;
        uint8_t y = scale8(sin8(beat), fb->height - 1);
        fb_set_pixel_rgb(fb, x, y, palette_color_rgb(p->palette, p->size, x * 7 + hue, 255, true));
    }

    return fb_end(fb);
}
