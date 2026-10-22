/**
 * Butterflies effect
 *
 * Author: SottNick, from GyverLamp (gunner47_v2): "Мотыльки" and "Лампа с мотыльками"
 * https://github.com/nikitakiselev/gunner47_v2
 *
 * Butterflies sit on the glass, then one by one fly along a curved path and land again.
 * In the lamp mode the matrix is a lamp of the chosen color and the butterflies are its shadows.
 */
#include "effects/butterflies.h"

#include <math.h>
#include <lib8tion.h>

#define MAX_BUTTERFLIES 100

#define P_LAMP       0
#define P_COUNT      1
#define P_SPEED      2
#define P_WINGS      3
#define P_HUE        4
#define P_SATURATION 5

EFFECT_PARAMS(butterflies, 6) = {
    DECL_PARAM_BOOL(P_LAMP, "lamp", "Lamp with butterflies", 0),
    DECL_PARAM_RANGE(P_COUNT, "butterflies", "Number of butterflies", 1, MAX_BUTTERFLIES, 20),
    DECL_PARAM_RANGE(P_SPEED, "speed", "Speed", 1, 255, 53),
    DECL_PARAM_BOOL(P_WINGS, "wings", "Flapping wings", 1),
    DECL_PARAM_RANGE(P_HUE, "hue", "Lamp hue", 0, 255, 31),
    DECL_PARAM_RANGE(P_SATURATION, "saturation", "Lamp saturation", 0, 255, 170),
};

#define SITTING 255 // state of a butterfly on the glass, smaller values: flying, rising to SITTING

typedef struct
{
    float x, y;
    float speed_x, speed_y;
    float turn; // turn of the speed vector per frame
    uint8_t hue;
    uint8_t state;
} butterfly_t;

static butterfly_t flies[MAX_BUTTERFLIES];
static uint8_t step = 0;

esp_err_t effect_butterflies_prepare(framebuffer_t *fb)
{
    for (size_t i = 0; i < MAX_BUTTERFLIES; i++)
    {
        flies[i].x = random8_to(fb->width);
        flies[i].y = random8_to(fb->height);
        flies[i].speed_x = flies[i].speed_y = flies[i].turn = 0;
        flies[i].hue = random8();
        flies[i].state = SITTING;
    }
    step = 0;
    return ESP_OK;
}

// turn the speed vector keeping |speed_x| + |speed_y|
static void turn(butterfly_t *b)
{
    float max = fabsf(b->speed_x) + fabsf(b->speed_y);
    if (max == fabsf(b->speed_x + b->speed_y))
    {
        if (b->speed_x > 0)
        {
            b->speed_x += b->turn;
            if (b->speed_x > max)
            {
                b->speed_x = max + max - b->speed_x;
                b->speed_y = b->speed_x - max;
            }
            else
                b->speed_y = max - fabsf(b->speed_x);
        }
        else
        {
            b->speed_x -= b->turn;
            if (b->speed_x + max < 0)
            {
                b->speed_x = -b->speed_x - max - max;
                b->speed_y = max - fabsf(b->speed_x);
            }
            else
                b->speed_y = fabsf(b->speed_x) - max;
        }
    }
    else
    {
        if (b->speed_x > 0)
        {
            b->speed_x -= b->turn;
            if (b->speed_x > max)
            {
                b->speed_x = max + max - b->speed_x;
                b->speed_y = max - b->speed_x;
            }
            else
                b->speed_y = fabsf(b->speed_x) - max;
        }
        else
        {
            b->speed_x += b->turn;
            if (b->speed_x + max < 0)
            {
                b->speed_x = -b->speed_x - max - max;
                b->speed_y = -b->speed_x - max;
            }
            else
                b->speed_y = max - fabsf(b->speed_x);
        }
    }
}

esp_err_t effect_butterflies_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    bool lamp = PARAM_VAL(butterflies, P_LAMP);
    bool wings = PARAM_VAL(butterflies, P_WINGS);
    size_t count = PARAM_VAL(butterflies, P_COUNT);
    float factor = PARAM_VAL(butterflies, P_SPEED) / 2048.0f + 0.001f;
    float w = fb->width, h = fb->height;

    if (wings && !lamp)
        fb_fade(fb, 255 - 35); // trails of the wings
    else
        fb_clear(fb);

    if (++step >= count)
        step = 0;

    for (size_t i = 0; i < count; i++)
    {
        butterfly_t *b = &flies[i];

        b->x += b->speed_x * factor;
        b->y += b->speed_y * factor;

        if (b->x < 0)
            b->x += w - 1;
        if (b->x > w - 1)
            b->x -= w - 1;
        if (b->y < 0)
        {
            b->y = -b->y;
            b->speed_y = -b->speed_y;
        }
        if (b->y > h - 1)
        {
            b->y = 2 * h - 2 - b->y;
            b->speed_y = -b->speed_y;
        }
        // tiny matrices: keep inside
        b->x = fminf(fmaxf(b->x, 0), w - 1);
        b->y = fminf(fmaxf(b->y, 0), h - 1);

        turn(b);

        if (b->state == SITTING)
        {
            // take off, one butterfly per frame at most
            if (step == i && random8_to(2) == 0)
            {
                b->state = random8_between(220, 244);
                b->speed_x = random8_to(101) / 20.0f + 1.0f;
                if (random8_to(2) == 0)
                    b->speed_x = -b->speed_x;
                b->speed_y = random8_to(101) / 20.0f + 1.0f;
                if (random8_to(2) == 0)
                    b->speed_y = -b->speed_y;
                b->turn = random8_to((fabsf(b->speed_x) + fabsf(b->speed_y)) * 20.0f + 2.0f) / 200.0f;
                if (random8_to(2) == 0)
                    b->turn = -b->turn;
            }
        }
        else
        {
            if (step == i)
                b->state++;
            // land on a whole pixel
            uint8_t t = SITTING - b->state;
            if (t == 0 || ((uint16_t)(b->x * t) % t == 0 && (uint16_t)(b->y * t) % t == 0))
            {
                b->x = roundf(b->x);
                b->y = roundf(b->y);
                b->speed_x = b->speed_y = b->turn = 0;
                b->state = SITTING;
            }
        }

        uint8_t v = b->state;
        if (wings)
            v = b->state == SITTING ? 255 : 128 + random8_to(2) * 111;
        fb_set_pixelf_hsv(fb, b->x, b->y, hsv_from_values(lamp ? 0 : b->hue, lamp ? 0 : 255, v));
    }

    if (!lamp)
    {
        // draw sitting butterflies again, sharp
        for (size_t i = 0; i < count; i++)
            if (flies[i].state == SITTING)
                fb_set_pixel_hsv(fb, flies[i].x, flies[i].y, hsv_from_values(flies[i].hue, 255, SITTING));
    }
    else
    {
        // invert: lamp with the butterflies as shadows
        for (size_t y = 0; y < fb->height; y++)
            for (size_t x = 0; x < fb->width; x++)
            {
                rgb_t c;
                fb_get_pixel_rgb(fb, x, y, &c);
                fb_set_pixel_hsv(fb, x, y, hsv_from_values(PARAM_VAL(butterflies, P_HUE),
                        PARAM_VAL(butterflies, P_SATURATION), 255 - c.r));
            }
    }

    return fb_end(fb);
}
