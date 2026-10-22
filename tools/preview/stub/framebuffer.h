// Host stub for tools/preview: just enough of the firmware headers to run the effects on a PC
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include <color.h>
#include <lib8tion.h>
typedef enum { FB_SHIFT_LEFT = 0, FB_SHIFT_RIGHT, FB_SHIFT_UP, FB_SHIFT_DOWN } fb_shift_direction_t;
typedef struct { size_t width, height; rgb_t *data; size_t frame_num; } framebuffer_t;
#define FB_OFFSET(fb, x, y) ((fb)->width * (y) + (x))
#define FB_SIZE(fb) ((fb)->width * (fb)->height * sizeof(rgb_t))
esp_err_t fb_set_pixel_rgb(framebuffer_t *fb, size_t x, size_t y, rgb_t c);
esp_err_t fb_set_pixel_hsv(framebuffer_t *fb, size_t x, size_t y, hsv_t c);
esp_err_t fb_set_pixelf_rgb(framebuffer_t *fb, float x, float y, rgb_t color);
esp_err_t fb_set_pixelf_hsv(framebuffer_t *fb, float x, float y, hsv_t color);
esp_err_t fb_get_pixel_rgb(framebuffer_t *fb, size_t x, size_t y, rgb_t *c);
esp_err_t fb_get_pixel_hsv(framebuffer_t *fb, size_t x, size_t y, hsv_t *c);
esp_err_t fb_clear(framebuffer_t *fb);
esp_err_t fb_shift(framebuffer_t *fb, size_t offs, fb_shift_direction_t dir);
esp_err_t fb_fade(framebuffer_t *fb, uint8_t s);
esp_err_t fb_blur2d(framebuffer_t *fb, fract8 amount);
static inline esp_err_t fb_begin(framebuffer_t *fb) { return 0; }
static inline esp_err_t fb_end(framebuffer_t *fb) { fb->frame_num++; return 0; }
