/**
 * Gyver fire: the classic fire of GyverLamp, flame from brightness and hue masks with sparks
 *
 * "Огонь" from GyverLamp by AlexGyver, the version of gunner47_v2:
 * https://github.com/AlexGyver/GyverLamp
 * https://github.com/nikitakiselev/gunner47_v2
 *
 * The masks are 8 rows high: the flame takes the bottom half of the matrix (rows of the masks
 * stretched or skipped to fit), the sparks fly in the upper half.
 */
#include "effects/gyver_fire.h"

#include <string.h>
#include <lib8tion.h>

#define P_HUE        0
#define P_SATURATION 1
#define P_SPARKS     2

EFFECT_PARAMS(gyver_fire, 4) = {
    DECL_PARAM_RANGE(P_HUE, "hue", "Hue", 0, 255, 0),
    DECL_PARAM_RANGE(P_SATURATION, "saturation", "Saturation", 0, 255, 255),
    DECL_PARAM_BOOL(P_SPARKS, "sparks", "Sparks", 1),
    DECL_PARAM_TEMPO(3, 50),
};

#define MASK_W      16
#define MASK_H      8
#define MAX_FLAME_H 16 // half of the largest matrix
#define SPARK_RATE  20 // a spark leaves the flame in a column once in about this many steps

// subtracted from the generated values to give a shape to the flame
static const uint8_t value_mask[MASK_H][MASK_W] = {
    { 0,   0,   0,   32,  32,  0,   0,   0,   0,   0,   0,   32,  32,  0,   0,   0   },
    { 0,   0,   0,   64,  64,  0,   0,   0,   0,   0,   0,   64,  64,  0,   0,   0   },
    { 0,   0,   32,  96,  96,  32,  0,   0,   0,   0,   32,  96,  96,  32,  0,   0   },
    { 0,   32,  64,  128, 128, 64,  32,  0,   0,   32,  64,  128, 128, 64,  32,  0   },
    { 32,  64,  96,  160, 160, 96,  64,  32,  32,  64,  96,  160, 160, 96,  64,  32  },
    { 64,  96,  128, 192, 192, 128, 96,  64,  64,  96,  128, 192, 192, 128, 96,  64  },
    { 96,  128, 160, 255, 255, 160, 128, 96,  96,  128, 160, 255, 255, 160, 128, 96  },
    { 128, 160, 192, 255, 255, 192, 160, 128, 128, 160, 192, 255, 255, 192, 160, 128 },
};

// hues of the flame from the base hue: 0 (red) to about 25 (yellow)
static const uint8_t hue_mask[MASK_H][MASK_W] = {
    { 25, 22, 11, 1, 1, 11, 19, 25, 25, 22, 11, 1, 1, 11, 19, 25 },
    { 25, 19, 8,  1, 1, 8,  13, 19, 25, 19, 8,  1, 1, 8,  13, 19 },
    { 19, 16, 8,  1, 1, 8,  13, 16, 19, 16, 8,  1, 1, 8,  13, 16 },
    { 13, 13, 5,  1, 1, 5,  11, 13, 13, 13, 5,  1, 1, 5,  11, 13 },
    { 11, 11, 5,  1, 1, 5,  11, 11, 11, 11, 5,  1, 1, 5,  11, 11 },
    { 8,  5,  1,  0, 0, 1,  5,  8,  8,  5,  1,  0, 0, 1,  5,  8  },
    { 5,  1,  0,  0, 0, 0,  1,  5,  5,  1,  0,  0, 0, 0,  1,  5  },
    { 1,  0,  0,  0, 0, 0,  0,  1,  1,  0,  0,  0, 0, 0,  0,  1  },
};

static size_t flame_h = MASK_H;
// key frames of the flame: the rows rise one step every other frame, the frame between is mixed
static uint8_t matrix_value[MAX_FLAME_H][MASK_W];
static uint8_t line[MASK_W];
// horizontal swing of the hues and of the brightness, row by row
static uint8_t shift_hue[MAX_FLAME_H * 2];
static uint8_t shift_value[MAX_FLAME_H * 2];
static uint8_t pcnt = 0;

static void generate_line()
{
    for (size_t x = 0; x < MASK_W; x++)
        line[x] = random8_between(127, 255);
}

static void shift_up()
{
    for (size_t y = flame_h - 1; y > 0; y--)
        memcpy(matrix_value[y], matrix_value[y - 1], MASK_W);
    memcpy(matrix_value[0], line, MASK_W);
}

static bool lit(framebuffer_t *fb, size_t x, size_t y, rgb_t *c)
{
    fb_get_pixel_rgb(fb, x, y, c);
    return c->r || c->g || c->b;
}

static void draw_flame_pixel(framebuffer_t *fb, size_t x, size_t y, int value, uint8_t hue_shift, uint8_t value_shift)
{
    size_t row = y * MASK_H / flame_h;
    value -= value_mask[row][(x + value_shift) % MASK_W];
    hsv_t c = {
        .h = PARAM_VAL(gyver_fire, P_HUE) + hue_mask[row][(x + hue_shift) % MASK_W],
        .s = PARAM_VAL(gyver_fire, P_SATURATION),
        .v = value < 0 ? 0 : value,
    };
    fb_set_pixel_hsv(fb, x, y, c);
}

// a swing of -1, 0 or +1 around 16, changing smoothly
static uint8_t swing(uint8_t prev, uint8_t rate)
{
    if (!random8_to(rate))
        return prev;
    int v = prev + random8_to(2) - random8_to(2);
    return v < 15 ? 15 : v > 17 ? 17 : v;
}

esp_err_t effect_gyver_fire_prepare(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));
    fb_clear(fb);
    CHECK(fb_end(fb));

    flame_h = MIN(MAX_FLAME_H, MAX(2, fb->height / 2));
    memset(matrix_value, 0, sizeof(matrix_value));
    memset(shift_hue, 16, sizeof(shift_hue));
    memset(shift_value, 16, sizeof(shift_value));
    generate_line();
    pcnt = 0;

    return ESP_OK;
}

esp_err_t effect_gyver_fire_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    if (pcnt >= 30)
    {
        shift_up();
        generate_line();
        pcnt = 0;
    }

    size_t h = MIN(fb->height, MAX_FLAME_H * 2);
    rgb_t c;

    // the bottom row mixes with the next line
    shift_hue[0] = swing(shift_hue[0], 2);
    shift_value[0] = swing(shift_value[0], 3);
    for (size_t x = 0; x < fb->width; x++)
    {
        size_t nx = x % MASK_W;
        draw_flame_pixel(fb, x, 0, ((100 - pcnt) * matrix_value[0][nx] + pcnt * line[nx]) / 100,
                shift_hue[0], shift_value[0]);
    }

    // every other row mixes with the one below it; from the top, so the sparks above the flame
    // move one row up from where they were
    for (size_t y = h - 1; y > 0; y--)
    {
        uint8_t hue_shift = shift_hue[y];
        uint8_t value_shift = shift_value[y];
        shift_hue[y] = shift_hue[y - 1];
        shift_value[y] = shift_value[y - 1];

        if (y < flame_h)
        {
            for (size_t x = 0; x < fb->width; x++)
            {
                size_t nx = x % MASK_W;
                draw_flame_pixel(fb, x, y, ((100 - pcnt) * matrix_value[y][nx] + pcnt * matrix_value[y - 1][nx]) / 100,
                        hue_shift, value_shift);
            }
            continue;
        }

        for (size_t x = 0; x < fb->width; x++)
            fb_set_pixel_rgb(fb, x, y, (rgb_t){ 0 });
        if (!PARAM_VAL(gyver_fire, P_SPARKS))
            continue;

        for (size_t x = 0; x < fb->width; x++)
        {
            if (y == flame_h)
            {
                // a spark leaves the top of the flame
                if (!random8_to(SPARK_RATE) && lit(fb, x, y - 1, &c))
                {
                    fb_get_pixel_rgb(fb, x, y - 2, &c);
                    fb_set_pixel_rgb(fb, x, y, c);
                }
            }
            else if (lit(fb, x, y - 1, &c))
            {
                // a spark rises, moving aside once in 4 steps
                size_t nx = random8_to(4) ? x : (x + fb->width + random8_to(2) - random8_to(2)) % fb->width;
                fb_set_pixel_rgb(fb, nx, y, c);
            }
        }
    }

    pcnt += 25;

    return fb_end(fb);
}
