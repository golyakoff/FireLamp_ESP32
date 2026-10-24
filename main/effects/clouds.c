/**
 * Clouds effect
 *
 * Noise with a palette, from the FastLED NoisePlusPalette example, GyverLamp (gunner47_v2): "Облака"
 * https://github.com/nikitakiselev/gunner47_v2
 *
 * White and blue clouds float slowly over the sky.
 */
#include "effects/clouds.h"

#include <string.h>
#include <noise.h>
#include <lib8tion.h>
#include "palettes.h"

#define P_SCALE 0
#define P_SPEED 1

EFFECT_PARAMS(clouds, 2) = {
    DECL_PARAM_RANGE(P_SCALE, "scale", "Scale", 1, 100, 34),
    DECL_PARAM_RANGE(P_SPEED, "speed", "Tempo", 1, 50, 4),
};

// the original ran at 20 frames per second: its speed per frame, scaled to 60, x256
#define SPEED_FACTOR (256 * 20 / 60)

static size_t size = 0;      // the larger side: the noise is square, read across and along
static uint8_t *noise = NULL; // size * size
static uint32_t nx, ny, nz;   // x256

esp_err_t effect_clouds_prepare(framebuffer_t *fb)
{
    size_t s = MAX(fb->width, fb->height);
    if (!noise || size != s)
    {
        free(noise);
        size = s;
        noise = calloc(size * size, 1);
        if (!noise)
        {
            size = 0;
            return ESP_ERR_NO_MEM;
        }
    }
    memset(noise, 0, size * size);
    nx = (uint32_t)random16() << 8;
    ny = (uint32_t)random16() << 8;
    nz = (uint32_t)random16() << 8;
    return ESP_OK;
}

esp_err_t effect_clouds_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    uint8_t scale = PARAM_VAL(clouds, P_SCALE);
    uint8_t speed = PARAM_VAL(clouds, P_SPEED);
    uint8_t smoothing = speed < 50 ? 200 - speed * 4 : 0;

    for (size_t i = 0; i < size; i++)
        for (size_t j = 0; j < size; j++)
        {
            uint8_t data = inoise8_3d((nx >> 8) + scale * i, (ny >> 8) + scale * j, nz >> 8);
            data = qsub8(data, 16);
            data = qadd8(data, scale8(data, 39));
            uint8_t *n = &noise[i * size + j];
            *n = smoothing ? scale8(*n, smoothing) + scale8(data, 256 - smoothing) : data;
        }
    nz += speed * SPEED_FACTOR;
    // a slow drift, for variety
    nx += speed / 8 * SPEED_FACTOR;
    ny -= speed / 16 * SPEED_FACTOR;

    const palette_def_t *p = &palettes[PALETTE_CLOUD];
    for (size_t x = 0; x < fb->width; x++)
        for (size_t y = 0; y < fb->height; y++)
        {
            uint8_t index = noise[y * size + x];
            uint8_t bri = noise[x * size + y];
            bri = bri > 127 ? 255 : dim8_raw(bri * 2);
            fb_set_pixel_rgb(fb, x, y, palette_color_rgb(p->palette, p->size, index, bri, true));
        }

    return fb_end(fb);
}
