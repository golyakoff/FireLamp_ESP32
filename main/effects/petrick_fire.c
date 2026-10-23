/**
 * Petrick fire: heat map rising over two layers of Perlin noise
 *
 * Author: Stefan Petrick
 * https://gist.github.com/StefanPetrick/819e873492f344ebebac5bcd2fdd8aa8
 * https://gist.github.com/StefanPetrick/1ba4584e534ba99ca259c1103754e4c5
 *
 * "Огонь 2018", adaptation (c) SottNick, GyverLamp gunner47_v2:
 * https://github.com/nikitakiselev/gunner47_v2
 *
 * The first noise layer seeds the bottom line and cools the rising heat, the second one dims the
 * picture. The heat map is kept top to bottom, as in the original.
 */
#include "effects/petrick_fire.h"

#include <string.h>
#include <esp_timer.h>
#include <lib8tion.h>
#include <noise.h>

#define P_SHADE 0

EFFECT_PARAMS(petrick_fire, 2) = {
    // green of the flame, percent of its red: red at 1, yellow at 100
    DECL_PARAM_RANGE(P_SHADE, "shade", "Shade", 1, 100, 15),
    DECL_PARAM_TEMPO(1, 100),
};

#define LAYERS 2

static size_t width = 0, height = 0;
// noise[layer][x][y], heat[y][x] with y = 0 at the top
static uint8_t *noise = NULL, *heat = NULL;

#define NOISE(l, x, y) noise[((l) * width + (x)) * height + (y)]
#define HEAT(x, y) heat[(y) * width + (x)]

static void fill_layer(uint8_t layer, uint16_t speed, uint16_t ctrl, uint16_t ctrl1, uint16_t ctrl2, uint32_t ms)
{
    uint32_t nx = 3 * ctrl * speed;
    uint32_t ny = 20 * ms * speed;
    uint32_t nz = 5 * ms * speed;
    uint32_t sx = ctrl1 / 2;
    uint32_t sy = ctrl2 / 2;
    int cx = width / 2 + width % 2;
    int cy = height / 2 + height % 2;

    for (size_t i = 0; i < width; i++)
    {
        uint32_t ioffset = sx * (uint32_t)((int)i - cx);
        for (size_t j = 0; j < height; j++)
        {
            uint32_t joffset = sy * (uint32_t)((int)j - cy);
            uint16_t data = inoise16_3d(nx + ioffset, ny + joffset, nz) + 1;
            NOISE(layer, i, j) = data >> 8;
        }
    }
}

esp_err_t effect_petrick_fire_prepare(framebuffer_t *fb)
{
    CHECK_ARG(fb);

    if (!noise || width != fb->width || height != fb->height)
    {
        free(noise);
        width = fb->width;
        height = fb->height;
        noise = calloc(width * height * (LAYERS + 1), 1);
        if (!noise)
        {
            width = height = 0;
            return ESP_ERR_NO_MEM;
        }
        heat = noise + LAYERS * width * height;
    }
    memset(heat, 0, width * height);

    return ESP_OK;
}

esp_err_t effect_petrick_fire_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    uint32_t ms = esp_timer_get_time() / 1000;

    // some changing values
    uint16_t ctrl1 = inoise16_3d(11 * ms, 0, 0);
    uint16_t ctrl2 = inoise16_3d(13 * ms, 100000, 100000);
    uint16_t ctrl = ((uint32_t)ctrl1 + ctrl2) / 2;

    // the heat map and the brightness mask
    fill_layer(0, 25, ctrl, ctrl1, ctrl2, ms);
    fill_layer(1, 20, ctrl, ctrl1, ctrl2, ms);

    int cy = height / 2 + height % 2;

    // seed the fire in the lowest line
    for (size_t x = 0; x < width; x++)
        HEAT(x, height - 1) = NOISE(0, width - 1 - x, cy - 1);

    // everything one line up
    memmove(heat, heat + width, width * (height - 1));

    // cool: a high noise value makes high flames
    for (size_t y = 0; y < height - 1; y++)
        for (size_t x = 0; x < width; x++)
            HEAT(x, y) = scale8(HEAT(x, y), 255 - (uint8_t)(NOISE(0, x, y) / 1.7f));

    uint8_t shade = PARAM_VAL(petrick_fire, P_SHADE);
    for (size_t y = 0; y < height; y++)
        for (size_t x = 0; x < width; x++)
        {
            uint8_t h = HEAT(x, y);
            uint8_t dim = NOISE(1, x, y);
            rgb_t c = {
                .r = scale8(h, dim),
                .g = scale8((uint8_t)(h * shade / 100), dim),
                .b = 0,
            };
            fb_set_pixel_rgb(fb, x, height - 1 - y, c);
        }

    return fb_end(fb);
}
