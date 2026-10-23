#include "effects/gif.h"
#include <nsgif.h>
#include <esp_heap_caps.h>

// TODO upload custom gif files in web browser, store in NVS

#define EMBED_GIF(name) \
    extern const uint8_t embed_##name##_start[] asm("_binary_"#name"_gif_start"); \
    extern const uint8_t embed_##name##_end[] asm("_binary_"#name"_gif_end")

#define USE_EMBED_GIF(name) { .start = embed_##name##_start, .end = embed_##name##_end }

EMBED_GIF(heart);
EMBED_GIF(heart2);
EMBED_GIF(boldman);
EMBED_GIF(coin);
EMBED_GIF(disk);
EMBED_GIF(girl);
EMBED_GIF(hairyman);
EMBED_GIF(hero);
EMBED_GIF(torch);
EMBED_GIF(zombie);
EMBED_GIF(cutebear);

typedef struct
{
    const uint8_t *start;
    const uint8_t *end;
} embed_gif_t;

static embed_gif_t gifs[] = {
    USE_EMBED_GIF(heart),
    USE_EMBED_GIF(heart2),
    USE_EMBED_GIF(boldman),
    USE_EMBED_GIF(coin),
    USE_EMBED_GIF(disk),
    USE_EMBED_GIF(girl),
    USE_EMBED_GIF(hairyman),
    USE_EMBED_GIF(hero),
    USE_EMBED_GIF(torch),
    USE_EMBED_GIF(zombie),
    USE_EMBED_GIF(cutebear),
};

// libnsgif is built with 10 bit LZW codes (components/libnsgif/CMakeLists.txt): 1024 table
// entries. Every code of a frame adds at most one entry to the 2^8 + 2 initial ones, and there are
// no more codes than pixels, so the frames up to 1024 - 258 pixels always fit.
#define GIF_MAX_PIXELS (1024 - 258)

#define P_FILE 0

EFFECT_PARAMS(gif, 2) = {
    DECL_PARAM_RANGE(P_FILE, "file", "File", 0, sizeof(gifs) / sizeof(embed_gif_t) - 1, 0),
    DECL_PARAM_TEMPO(1, 100),
};

nsgif_t *gif = NULL;
const nsgif_info_t *gif_info;
uint32_t next_frame_time = 0;
// the GIF is scaled to fit the matrix keeping the aspect ratio: out_w x out_h at pos_x, pos_y
size_t out_w = 0, out_h = 0, pos_x = 0, pos_y = 0;

static nsgif_bitmap_t *bitmap_create(int width, int height)
{
    return calloc(width * height, 4);
}

static void bitmap_destroy(nsgif_bitmap_t *bitmap)
{
    free(bitmap);
}

static uint8_t *bitmap_get_buffer(nsgif_bitmap_t *bitmap)
{
    return (uint8_t *)bitmap;
}

static void bitmap_set_opaque(void *bitmap, bool opaque) {}

static bool bitmap_test_opaque(void *bitmap)
{
    return false;
}

static void bitmap_mark_modified(void *bitmap) {}

static nsgif_bitmap_cb_vt bitmap_callbacks = {
    .create = bitmap_create,
    .destroy = bitmap_destroy,
    .get_buffer = bitmap_get_buffer,
    .set_opaque = bitmap_set_opaque,
    .test_opaque = bitmap_test_opaque,
    .modified = bitmap_mark_modified,
    .get_rowspan = NULL
};

esp_err_t effect_gif_prepare(framebuffer_t *fb)
{
    if (gif)
        nsgif_destroy(gif);

    nsgif_error res = nsgif_create(&bitmap_callbacks, NSGIF_BITMAP_FMT_ABGR8888, &gif);
    if (res != NSGIF_OK)
    {
        ESP_LOGE(TAG, "Error creating GIF handler: %d (%s)", res, nsgif_strerror(res));
        return ESP_FAIL;
    }

    size_t size = gifs[PARAM_VAL(gif, P_FILE)].end - gifs[PARAM_VAL(gif, P_FILE)].start;
    res = nsgif_data_scan(gif, size, (uint8_t *)gifs[PARAM_VAL(gif, P_FILE)].start);
    nsgif_data_complete(gif);

    if (res != NSGIF_OK)
    {
        nsgif_destroy(gif);
        gif = NULL;
        ESP_LOGE(TAG, "Error loading GIF: %d (%s), free heap %u, largest block %u", res, nsgif_strerror(res),
                heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        return ESP_FAIL;
    }

    gif_info = nsgif_get_info(gif);
    ESP_LOGI(TAG, "GIF frame_count=%lu, width=%lu, height=%lu", gif_info->frame_count, gif_info->width, gif_info->height);

    if (gif_info->width * gif_info->height > GIF_MAX_PIXELS)
    {
        nsgif_destroy(gif);
        gif = NULL;
        ESP_LOGE(TAG, "GIF is too large: %lux%lu, at most %d pixels", gif_info->width, gif_info->height, GIF_MAX_PIXELS);
        return ESP_FAIL;
    }

    if (gif_info->width * fb->height <= gif_info->height * fb->width)
    {
        out_h = fb->height;
        out_w = MAX(1, gif_info->width * fb->height / gif_info->height);
    }
    else
    {
        out_w = fb->width;
        out_h = MAX(1, gif_info->height * fb->width / gif_info->width);
    }
    pos_x = (fb->width - out_w) / 2;
    pos_y = (fb->height - out_h) / 2;

    next_frame_time = 0;

    return ESP_OK;
}

esp_err_t effect_gif_run(framebuffer_t *fb)
{
    if (!gif)
        return ESP_OK;

    uint32_t time_cs = esp_timer_get_time() / 10000;
    if (time_cs < next_frame_time)
        return ESP_OK;
    
    nsgif_rect_t frame_rect;
    uint32_t delay_cs;
    uint32_t frame;
    nsgif_error res = nsgif_frame_prepare(gif, &frame_rect, &delay_cs, &frame);
    if (res == NSGIF_ERR_ANIMATION_END)
    {
        nsgif_reset(gif);
        res = nsgif_frame_prepare(gif, &frame_rect, &delay_cs, &frame);
    }
    if (res != NSGIF_OK)
    {
        ESP_LOGW(TAG, "Error preparing GIF frame: %d (%s)", res, nsgif_strerror(res));
        return ESP_FAIL;
    }
    next_frame_time = delay_cs == NSGIF_INFINITE ? UINT32_MAX : time_cs + delay_cs - 1;

    nsgif_bitmap_t *bitmap;
    res = nsgif_frame_decode(gif, frame, &bitmap);
    if (res != NSGIF_OK)
    {
        ESP_LOGW(TAG, "Error decoding GIF frame: %d (%s)", res, nsgif_strerror(res));
        return ESP_FAIL;
    }

    CHECK(fb_begin(fb));

    // the bitmap is the whole composed image, not only frame_rect: draw all of it.
    // Every matrix pixel gets the average of its block of GIF pixels (one pixel when scaling up).
    // GIF rows go top to bottom, matrix rows bottom to top.
    uint32_t *frame_image = (uint32_t *)bitmap;

    fb_clear(fb);
    for (size_t oy = 0; oy < out_h; oy++)
    {
        size_t y0 = oy * gif_info->height / out_h;
        size_t y1 = MAX(y0 + 1, (oy + 1) * gif_info->height / out_h);
        for (size_t ox = 0; ox < out_w; ox++)
        {
            size_t x0 = ox * gif_info->width / out_w;
            size_t x1 = MAX(x0 + 1, (ox + 1) * gif_info->width / out_w);
            uint32_t r = 0, g = 0, b = 0;
            for (size_t y = y0; y < y1; y++)
                for (size_t x = x0; x < x1; x++)
                {
                    uint32_t p = frame_image[y * gif_info->width + x];
                    r += p & 0xff;
                    g += (p >> 8) & 0xff;
                    b += (p >> 16) & 0xff;
                }
            uint32_t n = (y1 - y0) * (x1 - x0);
            rgb_t c = { .r = r / n, .g = g / n, .b = b / n };
            fb_set_pixel_rgb(fb, pos_x + ox, pos_y + out_h - oy - 1, c);
        }
    }

    return fb_end(fb);
}
