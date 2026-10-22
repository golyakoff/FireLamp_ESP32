/**
 * Tunnel effect
 *
 * Original Lua/TIC-80 source was live-coded in 25 minutes by exoticorn during a 256-byte battle
 * at outlinedemoparty.nl 2021-05-15
 *
 * Author: exoticorn, Ported by: sutarobosu (https://wokwi.com/projects/332710874737279572)
 *
 * Fixed-point rewrite: everything that does not depend on time (20 / r, 9 / y and the "near"
 * test) is precomputed in prepare(), the per-frame loop uses Q8 integer math only, so the
 * effect does not depend on an FPU (ESP32-C3 has none).
 */
#include "effects/tunnel.h"

#include <math.h>
#include <esp_timer.h>

#define P_SPEED 0

EFFECT_PARAMS(tunnel, 1) = {
    DECL_PARAM_RANGE(P_SPEED, "speed", "Tempo", 1, 170, 70),
};

#define FP_SHIFT 8
#define FP_ONE   (1 << FP_SHIFT)
#define CENTER   0xffff

static const uint32_t pal[] = {
    0x1a1c2c, 0x5d275d, 0xb13e53, 0xef7d57,
    0xffcd75, 0xa7f070, 0x38b764, 0x257179, 0x29366f, 0x3b5dc9,
    0x41a6f6, 0x73eff7, 0xf4f4f4, 0x94b0c2, 0x566c86, 0x333c57
};

static uint8_t half_width = 0;
static int8_t y_first = 0;
static int8_t y_last = 0;
static rgb_t *last = NULL;
static uint16_t *dist = NULL;     // 20 / r for each pixel of the left half, Q8; CENTER for r == 0
static uint16_t *row_w = NULL;    // 9 / y for each row (y > 0 only), Q8
static uint8_t *near_mask = NULL; // (x * 9 / y)^2 < 39 for each pixel of the left half

static inline uint8_t far_color(int y)
{
    return 9 - y / 3 - ((0x2 >> (-y % 3)) & 1);
}

esp_err_t effect_tunnel_prepare(framebuffer_t *fb)
{
    CHECK_ARG(fb);

    uint8_t half_height = fb->height / 2;
    half_width = fb->width / 2;
    y_first = -half_height - half_height / 8;
    y_last = half_height - half_height / 8;
    last = fb->data + fb->height * fb->width - 1;

    size_t rows = y_last - y_first;
    size_t cols = half_width + 1;

    if (!dist) dist = calloc(rows * cols, sizeof(uint16_t));
    if (!row_w) row_w = calloc(rows, sizeof(uint16_t));
    if (!near_mask) near_mask = calloc(rows * cols, 1);
    if (!dist || !row_w || !near_mask) return ESP_ERR_NO_MEM;

    for (int y = y_first; y < y_last; y++)
    {
        size_t r = y - y_first;
        row_w[r] = y > 0 ? (uint16_t)lroundf(9.0f * FP_ONE / y) : 0;
        for (int x = -half_width; x <= 0; x++)
        {
            size_t i = r * cols + x + half_width;
            dist[i] = (x || y) ? (uint16_t)lroundf(20.0f * FP_ONE / sqrtf(x * x + y * y)) : CENTER;
            near_mask[i] = y > 0 && x * x * 81 < 39 * y * y;
        }
    }

    return ESP_OK;
}

esp_err_t effect_tunnel_run(framebuffer_t *fb)
{
    CHECK(fb_begin(fb));

    uint32_t div = 201 - PARAM_VAL(tunnel, P_SPEED);
    int32_t t = (int32_t)((((uint32_t)(esp_timer_get_time() / 1000) % (42 * div)) << FP_SHIFT) / div);

    rgb_t *pixel = last;
    const uint16_t *d = dist;
    const uint8_t *nm = near_mask;

    for (int y = y_first; y < y_last; y++)
    {
        int32_t w = row_w[y - y_first]; // w - t
        for (int x = -half_width; x <= 0; x++, d++, nm++)
        {
            uint8_t c;
            if (*d == CENTER)
            {
                // r == 0: the float original runs into inf/NaN here and always ends up with far_color(0)
                *pixel-- = rgb_from_code(pal[far_color(0)]);
                continue;
            }
            int32_t z = *d + t;
            int32_t zm = z % (21 * FP_ONE);
            int32_t q = zm < 15 * FP_ONE ? *d - zm + 15 * FP_ONE : *d; // q - t
            if (y > 0 && w < q)
                c = (*nm ? 14 : 6) + (((w + t) >> FP_SHIFT) & 1);
            else
            {
                // -y * q < 99 / ((x * q / 50)^2 + 1)  <=>  -y * q * ((x * q / 50)^2 + 1) < 99
                int32_t a = x * q / 50;
                // 64 bit: overflows int32 on matrices larger than 16x16
                int64_t lhs = ((int64_t)(-y * q) * ((a * a >> FP_SHIFT) + FP_ONE)) >> FP_SHIFT;
                if (lhs < 99 * FP_ONE)
                    c = zm >= 15 * FP_ONE ? ((z >> FP_SHIFT) & 1) : 3;
                else
                    c = far_color(y);
            }
            *pixel-- = rgb_from_code(pal[c]);
        }
        rgb_t *src = pixel + 1;
        for (uint8_t x = 1; x < half_width; x++)
            *pixel-- = *++src;
    }

    return fb_end(fb);
}
