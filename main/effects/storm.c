/**
 * Thunderstorm effect: rain with clouds and lightning
 *
 * "Гроза в банке" from GyverLamp (gunner47_v2), based on the Sublime demos by Marc Merlin
 * https://github.com/nikitakiselev/gunner47_v2
 * https://github.com/marcmerlin/FastLED_NeoMatrix_SmartMatrix_LEDMatrix_GFX_Demos
 *
 * Lightning is reworked: the original bolt map was not initialized and a bolt lasted one frame.
 * Here a bolt lives a few frames, flickers and lights up the clouds.
 */
#include "effects/storm.h"
#include "palettes.h"

#include <string.h>
#include <esp_timer.h>
#include <lib8tion.h>
#include <noise.h>

#define P_DENSITY   0
#define P_LIGHTNING 1
#define P_CLOUDS    2

EFFECT_PARAMS(storm, 3) = {
    DECL_PARAM_RANGE(P_DENSITY, "density", "Density", 1, 100, 25),
    DECL_PARAM_RANGE(P_LIGHTNING, "lightning", "Lightning frequency", 0, 100, 50),
    DECL_PARAM_BOOL(P_CLOUDS, "clouds", "Clouds", 1),
};

#define DROP_MIN       60  // drop brightness, cells below are empty
#define DROP_MAX       160
#define TAIL           30  // brightness kept from the previous frame, of 256
#define CLOUD_SCALE    250 // noise scale of the clouds
#define CLOUD_SMOOTH   192
#define BOLT_FADE      140 // bolt brightness kept per frame, of 256
#define FLASH_FADE     200
#define MAX_BRANCHES   3

static const rgb_t RAIN_COLOR  = { .r = 60, .g = 80, .b = 90 };
static const rgb_t BOLT_COLOR  = { .r = 220, .g = 220, .b = 255 };
static const rgb_t FLASH_COLOR = { .r = 50, .g = 50, .b = 60 };

// cloud palette: black, dark cyan, darker cyan, black
static const rgb_t CLOUD_PALETTE[] = {
    { .r = 0, .g = 0, .b = 0 },
    { .r = 15, .g = 24, .b = 24 },
    { .r = 9, .g = 15, .b = 15 },
    { .r = 0, .g = 0, .b = 0 },
};

static size_t width = 0, height = 0, cloud_h = 0;
static uint8_t *drops = NULL;  // width * height, brightness of the rain drop
static uint8_t *bolt = NULL;   // width * height, brightness of the lightning
static uint8_t *clouds = NULL; // width * cloud_h, smoothed noise
static uint8_t *splash = NULL; // width, drop that hit the ground on the previous frame
static uint8_t *heads = NULL;  // width, bolt heads in the current line
static uint8_t flash = 0;

#define CELL(a, x, y) (a)[(y) * width + (x)]

esp_err_t effect_storm_prepare(framebuffer_t *fb)
{
    CHECK_ARG(fb);

    if (!drops || width != fb->width || height != fb->height)
    {
        free(drops);
        width = fb->width;
        height = fb->height;
        cloud_h = height * 2 / 5 + 1;
        drops = calloc(width * (height * 2 + cloud_h + 2), 1);
        if (!drops)
        {
            width = height = 0;
            return ESP_ERR_NO_MEM;
        }
        bolt = drops + width * height;
        clouds = bolt + width * height;
        splash = clouds + width * cloud_h;
        heads = splash + width;
    }
    memset(drops, 0, width * (height * 2 + cloud_h + 2));
    flash = 0;

    return ESP_OK;
}

static inline rgb_t rain_color(uint8_t v)
{
    return rgb_scale(RAIN_COLOR, v);
}

static void strike()
{
    memset(heads, 0, width);
    size_t x = random8_to(width);
    heads[x] = 1;
    CELL(bolt, x, height - 1) = 255;

    // random walk down, sometimes forking
    for (size_t y = height - 1; y > 1; y--)
    {
        uint8_t next[width];
        memset(next, 0, width);
        size_t branches = 0;
        for (x = 0; x < width; x++)
            branches += heads[x];
        for (x = 0; x < width; x++)
        {
            if (!heads[x])
                continue;
            size_t l = x > 0 ? x - 1 : x;
            size_t r = x < width - 1 ? x + 1 : x;
            uint8_t dir = random8_to(16);
            if (dir == 0 && branches < MAX_BRANCHES && l != r)
            {
                next[l] = next[r] = 1;
                branches++;
            }
            else if (dir < 6)
                next[l] = 1;
            else if (dir < 11)
                next[r] = 1;
            else
                next[x] = 1;
        }
        memcpy(heads, next, width);
        for (x = 0; x < width; x++)
            if (heads[x])
                CELL(bolt, x, y - 1) = 255;
    }
    flash = 255;
}

esp_err_t effect_storm_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    fb_fade(fb, 255 - TAIL);

    uint8_t spawn = (PARAM_VAL(storm, P_DENSITY) - 1) * 258 / 100;

    for (size_t x = 0; x < width; x++)
    {
        // move drops down
        for (size_t y = 0; y < height; y++)
        {
            if (CELL(drops, x, y) < DROP_MIN)
                continue;
            if (y > 0)
                CELL(drops, x, y - 1) = CELL(drops, x, y);
            CELL(drops, x, y) = 0;
        }

        // new drops at the top
        if (random8() < spawn)
            CELL(drops, x, height - 1) = random8_between(DROP_MIN, DROP_MAX);

        for (size_t y = 0; y < height; y++)
            if (CELL(drops, x, y) >= DROP_MIN)
                fb_set_pixel_rgb(fb, x, y, rain_color(CELL(drops, x, y)));

        // splashes
        uint8_t j = splash[x];
        uint8_t v = CELL(drops, x, 0);
        if (j >= DROP_MIN)
        {
            fb_set_pixel_rgb(fb, (x + width - 2) % width, 0, rain_color(j / 3));
            fb_set_pixel_rgb(fb, (x + 2) % width, 0, rain_color(j / 3));
            splash[x] = 0;
        }
        if (v >= DROP_MIN && height > 1)
        {
            fb_set_pixel_rgb(fb, (x + width - 1) % width, 1, rain_color(v / 2));
            fb_set_pixel_rgb(fb, (x + 1) % width, 1, rain_color(v / 2));
            splash[x] = v;
        }
    }

    // lightning: from about one strike in 2 seconds at 30 FPS down to none
    if (random16() < PARAM_VAL(storm, P_LIGHTNING) * 11)
        strike();
    else if (flash > 100 && !random8_to(4))
    {
        // flicker of the fading bolt
        for (size_t i = 0; i < width * height; i++)
            if (bolt[i])
                bolt[i] = 200;
        flash = 200;
    }

    if (PARAM_VAL(storm, P_CLOUDS))
    {
        uint32_t t = esp_timer_get_time() / 1000;
        rgb_t lit = rgb_scale(FLASH_COLOR, flash);
        for (size_t x = 0; x < width; x++)
            for (size_t z = 0; z < cloud_h; z++)
            {
                uint8_t n = qsub8(inoise8_3d(x * CLOUD_SCALE + t / 4, z * CLOUD_SCALE, t / 8), 16);
                n = qadd8(n, scale8(n, 39));
                uint8_t *c = &CELL(clouds, x, z);
                *c = scale8(*c, CLOUD_SMOOTH) + scale8(n, 256 - CLOUD_SMOOTH);

                size_t y = height - 1 - z;
                rgb_t p;
                fb_get_pixel_rgb(fb, x, y, &p);
                rgb_t cloud = palette_color_rgb(CLOUD_PALETTE, 4, *c, 255, true);
                cloud = rgb_add_rgb(cloud, rgb_scale(lit, *c));
                fb_set_pixel_rgb(fb, x, y, rgb_blend(p, cloud, (cloud_h - z) * (250 / cloud_h)));
            }
    }
    else if (flash)
    {
        // no clouds: the sky flashes
        rgb_t lit = rgb_scale(FLASH_COLOR, flash / 2);
        for (size_t y = 0; y < height; y++)
            for (size_t x = 0; x < width; x++)
            {
                rgb_t p;
                fb_get_pixel_rgb(fb, x, y, &p);
                fb_set_pixel_rgb(fb, x, y, rgb_add_rgb(p, lit));
            }
    }

    // bolt over everything
    for (size_t y = 0; y < height; y++)
        for (size_t x = 0; x < width; x++)
        {
            uint8_t b = CELL(bolt, x, y);
            if (!b)
                continue;
            rgb_t p;
            fb_get_pixel_rgb(fb, x, y, &p);
            fb_set_pixel_rgb(fb, x, y, rgb_add_rgb(p, rgb_scale(BOLT_COLOR, b)));
            CELL(bolt, x, y) = scale8(b, BOLT_FADE);
        }
    flash = scale8(flash, FLASH_FADE);

    return fb_end(fb);
}
