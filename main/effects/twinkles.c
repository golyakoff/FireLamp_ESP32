/**
 * Twinkles effect
 *
 * Author: SottNick, from GyverLamp (gunner47_v2)
 * https://github.com/nikitakiselev/gunner47_v2
 *
 * Pixels smoothly light up and fade out, each at its own speed. A new pixel lights up only
 * when another one went out, so the number of lit pixels stays the same.
 */
#include "effects/twinkles.h"

#include <string.h>
#include <lib8tion.h>
#include "palettes.h"

#define SPEEDS     4 // speeds of the brightness change; 1..SPEEDS rise, SPEEDS+1..2*SPEEDS fade
#define MULTIPLIER 6 // added to every step, the slowest speed is too slow otherwise

#define PALETTE_RAINBOW PALETTE_MAX // the last value of the palette parameter: all hues

#define P_PALETTE 0
#define P_DENSITY 1

EFFECT_PARAMS(twinkles, 2) = {
    DECL_PARAM_RANGE(P_PALETTE, "palette", "Palette", 0, PALETTE_RAINBOW, PALETTE_RAINBOW),
    DECL_PARAM_RANGE(P_DENSITY, "density", "Density", 1, 11, 8),
};

typedef struct
{
    uint8_t hue;
    uint8_t speed; // and direction
    uint8_t value;
} twinkle_t;

static twinkle_t *pixels = NULL;
static size_t count = 0;
static size_t off = 0; // pixels that went out and may light up again

esp_err_t effect_twinkles_prepare(framebuffer_t *fb)
{
    CHECK_ARG(fb);

    if (!pixels || count != fb->width * fb->height)
    {
        free(pixels);
        count = fb->width * fb->height;
        pixels = calloc(count, sizeof(twinkle_t));
        if (!pixels)
        {
            count = 0;
            return ESP_ERR_NO_MEM;
        }
    }

    // a pixel is lit with the probability 1 / chance
    uint8_t chance = 12 - PARAM_VAL(twinkles, P_DENSITY);
    for (size_t i = 0; i < count; i++)
    {
        if (random8_to(chance) == 0)
        {
            pixels[i].hue = random8();
            pixels[i].speed = random8_between(1, SPEEDS * 2 + 1);
            pixels[i].value = random8();
        }
        else
            memset(&pixels[i], 0, sizeof(twinkle_t));
    }
    off = 0;

    return ESP_OK;
}

esp_err_t effect_twinkles_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    uint8_t chance = 12 - PARAM_VAL(twinkles, P_DENSITY);
    uint8_t pal_num = PARAM_VAL(twinkles, P_PALETTE);
    const palette_def_t *pal = pal_num < PALETTE_MAX ? &palettes[pal_num] : NULL;

    for (size_t i = 0; i < count; i++)
    {
        twinkle_t *p = &pixels[i];
        if (!p->value)
        {
            if (off && random8_to(chance) == 0)
            {
                p->hue = random8();
                p->speed = random8_between(1, SPEEDS + 1);
                p->value = p->speed;
                off--;
            }
        }
        else if (p->speed <= SPEEDS)
        {
            // rising
            if (p->value > 255 - p->speed - MULTIPLIER)
            {
                p->value = 255;
                p->speed += SPEEDS;
            }
            else
                p->value += p->speed + MULTIPLIER;
        }
        else
        {
            // fading
            if (p->value <= p->speed - SPEEDS + MULTIPLIER)
            {
                p->value = 0;
                off++;
            }
            else
                p->value -= p->speed - SPEEDS + MULTIPLIER;
        }

        rgb_t c = { 0 };
        if (p->value && pal)
            c = palette_color_rgb(pal->palette, pal->size, p->hue, p->value, true);
        else if (p->value)
            c = hsv2rgb_rainbow(hsv_from_values(p->hue, 255, p->value));
        fb_set_pixel_rgb(fb, i % fb->width, i / fb->width, c);
    }

    return fb_end(fb);
}
