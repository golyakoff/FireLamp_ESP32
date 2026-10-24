/**
 * Noise streams effects: rainbow snake, soaring fire, top fire
 *
 * NoiseSmearing by Stefan Petrick, GyverLamp mod by PalPalych, from gunner47_v2:
 * "Радужный змей" (MultipleStream8), "Парящий огонь" (MultipleStream3) and "Верховой огонь"
 * (MultipleStream5)
 * https://github.com/nikitakiselev/gunner47_v2
 *
 * A few bright dots are drawn every frame, then the picture is smeared by rows and columns,
 * each shifted by a smoothly changing noise value.
 */
#include "effects/streams.h"

#include <string.h>
#include <math.h>
#include <lib8tion.h>
#include <noise.h>

#define NOISE_SMOOTH 200
#define NOISE_STEP   3000
#define NOISE_SCALE  8000

#define P_HUE    0
#define P_ROTATE 1

EFFECT_PARAMS(rainbow_snake, 3) = {
    DECL_PARAM_RANGE(P_HUE, "hue", "Hue", 0, 255, 0),
    DECL_PARAM_BOOL(P_ROTATE, "rotate", "Rotate colors", 1),
    DECL_PARAM_TEMPO(2, 40),
};

#define P_FADEOUT 0

EFFECT_PARAMS(soaring_fire, 2) = {
    DECL_PARAM_RANGE(P_FADEOUT, "fadeout", "Fadeout", 1, 100, 15),
    DECL_PARAM_TEMPO(1, 40),
};

EFFECT_PARAMS(top_fire, 2) = {
    DECL_PARAM_RANGE(P_FADEOUT, "fadeout", "Fadeout", 1, 100, 15),
    DECL_PARAM_TEMPO(1, 25),
};

static size_t width = 0, height = 0;
// the effects were tuned for 16x16: the noise and the smearing scale with the smaller side
static int size = 16;
static uint8_t *noise = NULL; // width * height
static rgb_t *buf = NULL;     // width * height
static uint32_t nx, ny, nz;
static uint8_t hue;

static esp_err_t streams_prepare(framebuffer_t *fb)
{
    CHECK_ARG(fb);

    if (!noise || width != fb->width || height != fb->height)
    {
        free(noise);
        free(buf);
        width = fb->width;
        height = fb->height;
        size = MIN(width, height);
        noise = calloc(width * height, 1);
        buf = calloc(width * height, sizeof(rgb_t));
        if (!noise || !buf)
        {
            free(noise);
            free(buf);
            noise = NULL;
            buf = NULL;
            width = height = 0;
            return ESP_ERR_NO_MEM;
        }
    }
    memset(noise, 0, width * height);
    nx = random16();
    ny = random16();
    nz = random16();

    return ESP_OK;
}

static void fill_noise()
{
    nx += NOISE_STEP;
    ny += NOISE_STEP;
    nz += NOISE_STEP;
    for (size_t x = 0; x < width; x++)
    {
        int32_t xo = NOISE_SCALE * 16 / size * ((int32_t)x - (int32_t)width / 2);
        for (size_t y = 0; y < height; y++)
        {
            int32_t yo = NOISE_SCALE * 16 / size * ((int32_t)y - (int32_t)height / 2);
            uint8_t data = inoise16_3d(nx + xo, ny + yo, nz) >> 8;
            uint8_t *n = &noise[y * width + x];
            *n = scale8(*n, NOISE_SMOOTH) + scale8(data, 255 - NOISE_SMOOTH);
        }
    }
}

static rgb_t pixel(framebuffer_t *fb, int x, int y)
{
    rgb_t c = { 0 };
    if (x >= 0 && x < (int)width && y >= 0 && y < (int)height)
        fb_get_pixel_rgb(fb, x, y, &c);
    return c;
}

static rgb_t mix(rgb_t a, rgb_t b, uint8_t fraction)
{
    return rgb_add_rgb(rgb_scale(a, ease8InOutApprox(255 - fraction)), rgb_scale(b, ease8InOutApprox(fraction)));
}

// shift every row by the noise of its first pixel
static void smear_x(framebuffer_t *fb, int amplitude, float shift)
{
    for (size_t y = 0; y < height; y++)
    {
        int amount = ((int)noise[y * width] - 128) * 2 * amplitude * size / 16 + (int)(shift * 256);
        int delta = abs(amount) >> 8;
        uint8_t fraction = abs(amount) & 255;
        for (size_t x = 0; x < width; x++)
        {
            int a = amount < 0 ? (int)x - delta : (int)x + delta;
            int b = amount < 0 ? a - 1 : a + 1;
            buf[y * width + x] = mix(pixel(fb, a, y), pixel(fb, b, y), fraction);
        }
    }
    for (size_t y = 0; y < height; y++)
        for (size_t x = 0; x < width; x++)
            fb_set_pixel_rgb(fb, x, y, buf[y * width + x]);
}

// shift every column by the noise of its bottom pixel
static void smear_y(framebuffer_t *fb, int amplitude, float shift)
{
    for (size_t x = 0; x < width; x++)
    {
        int amount = ((int)noise[x] - 128) * 2 * amplitude * size / 16 + (int)(shift * 256);
        int delta = abs(amount) >> 8;
        uint8_t fraction = abs(amount) & 255;
        for (size_t y = 0; y < height; y++)
        {
            int a = amount < 0 ? (int)y - delta : (int)y + delta;
            int b = amount < 0 ? a - 1 : a + 1;
            buf[y * width + x] = mix(pixel(fb, x, a), pixel(fb, x, b), fraction);
        }
    }
    for (size_t y = 0; y < height; y++)
        for (size_t x = 0; x < width; x++)
            fb_set_pixel_rgb(fb, x, y, buf[y * width + x]);
}

static void add_hsv(framebuffer_t *fb, size_t x, size_t y, uint8_t h)
{
    rgb_t c;
    if (fb_get_pixel_rgb(fb, x, y, &c) == ESP_OK)
        fb_set_pixel_rgb(fb, x, y, rgb_add_rgb(c, hsv2rgb_rainbow(hsv_from_values(h, 255, 255))));
}

////////////////////////////////////////////////////////////////////////////////

esp_err_t effect_rainbow_snake_prepare(framebuffer_t *fb)
{
    hue = PARAM_VAL(rainbow_snake, P_HUE);
    return streams_prepare(fb);
}

esp_err_t effect_rainbow_snake_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    if (PARAM_VAL(rainbow_snake, P_ROTATE))
        hue++;
    else
        hue = PARAM_VAL(rainbow_snake, P_HUE);

    fb_fade(fb, 255 - 96);
    // a 3x3 grid of squares at any size, as on 16x16: 2x2 squares at 2, 7, 12 there, single
    // pixels at 1, 4, 6 on 8x8; the colors are those of the 16x16 positions
    int sq = MAX(1, size / 8);
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++)
        {
            int x0 = MIN(MAX((int)lroundf((2 * i + 1) * width / 6.0f - sq / 2.0f), 0), (int)width - sq);
            int y0 = MIN(MAX((int)lroundf((2 * j + 1) * height / 6.0f - sq / 2.0f), 0), (int)height - sq);
            int vx = x0 * 16 / (int)width, vy = y0 * 16 / (int)height;
            for (int dy = 0; dy < sq; dy++)
                for (int dx = 0; dx < sq; dx++)
                    add_hsv(fb, x0 + dx, y0 + dy, (vx + dx * 8 / sq) * (vy + dy * 8 / sq) + hue);
        }

    fill_noise();
    smear_x(fb, 3, 0);
    smear_y(fb, 3, 0);

    return fb_end(fb);
}

////////////////////////////////////////////////////////////////////////////////

esp_err_t effect_soaring_fire_prepare(framebuffer_t *fb)
{
    return streams_prepare(fb);
}

esp_err_t effect_soaring_fire_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    fb_blur2d(fb, 20);
    fb_fade(fb, PARAM_VAL(soaring_fire, P_FADEOUT) * 2);
    // a line of dots in the middle
    for (size_t x = 1; x < width; x += 3)
        add_hsv(fb, x, height / 2 - ((height - 1) & 1), x * 2);

    fill_noise();
    smear_y(fb, 3, 0);
    smear_x(fb, 3, 0);

    return fb_end(fb);
}

////////////////////////////////////////////////////////////////////////////////

esp_err_t effect_top_fire_prepare(framebuffer_t *fb)
{
    return streams_prepare(fb);
}

esp_err_t effect_top_fire_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    fb_blur2d(fb, 20);
    fb_fade(fb, PARAM_VAL(top_fire, P_FADEOUT) * 2);
    // dots along the top line, the picture drifts down
    for (size_t x = 1; x < width; x += 2)
        add_hsv(fb, x, height - 1, x * 2);

    fill_noise();
    smear_y(fb, 2, 1);
    smear_x(fb, 2, 0);

    return fb_end(fb);
}
