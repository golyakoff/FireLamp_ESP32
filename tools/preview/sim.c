// Runs one effect on a PC and writes the captured frames as raw RGB.
// sim CODE WIDTH HEIGHT FPS WARMUP STEP FRAMES OUT [param=value ...]
// sim --list
// sim --params CODE: the parameters as CODE MIN MAX DEFAULT lines
// sim --peak CODE WIDTH HEIGHT FPS FRAMES [param=value ...]: the brightest frame, per mille of the
//     full white matrix (sum of all the channels)
#include <assert.h>
#include "effect.h"
int64_t sim_time_us;
extern const effect_descriptor_t effects[];
extern const size_t effects_count;
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        for (size_t i = 0; i < effects_count; i++) printf("%s\t%s\n", effects[i].code, effects[i].name);
        return 0;
    }
    bool peak = argc > 2 && !strcmp(argv[1], "--peak"), params = argc == 3 && !strcmp(argv[1], "--params");
    if (peak || params) { argv++; argc--; }
    size_t e = 0; while (e < effects_count && strcmp(effects[e].code, argv[1])) e++;
    if (e == effects_count) { fprintf(stderr, "no effect %s\n", argv[1]); return 1; }
    if (params) {
        for (size_t p = 0; p < effects[e].params_count; p++)
            printf("%s %d %d %d\n", effects[e].params[p].code, effects[e].params[p].min, effects[e].params[p].max, effects[e].params[p].def);
        return 0;
    }
    if (peak) {
        size_t w = atoi(argv[2]), h = atoi(argv[3]); int fps = atoi(argv[4]), frames = atoi(argv[5]);
        for (int a = 6; a < argc; a++) {
            char *eq = strchr(argv[a], '='); *eq = 0;
            for (size_t p = 0; p < effects[e].params_count; p++) if (!strcmp(effects[e].params[p].code, argv[a])) effects[e].params[p].value = atoi(eq + 1);
        }
        framebuffer_t fbs = { w, h, calloc(w * h, sizeof(rgb_t)), 0 }, *fb = &fbs;
        sim_time_us = 1000000;
        if (effects[e].prepare) assert(effects[e].prepare(fb) == ESP_OK);
        uint64_t max = 0;
        for (int f = 0; f < frames; f++, sim_time_us += 1000000 / fps) {
            assert(effects[e].run(fb) == ESP_OK);
            uint64_t sum = 0;
            for (size_t i = 0; i < w * h; i++) sum += fb->data[i].r + fb->data[i].g + fb->data[i].b;
            if (sum > max) max = sum;
        }
        printf("%llu\n", (unsigned long long)((max * 1000 + w * h * 765 - 1) / (w * h * 765)));
        return 0;
    }
    size_t w = atoi(argv[2]), h = atoi(argv[3]); int fps = atoi(argv[4]), warm = atoi(argv[5]), step = atoi(argv[6]), frames = atoi(argv[7]);
    for (int a = 9; a < argc; a++) {
        char *eq = strchr(argv[a], '='); *eq = 0;
        for (size_t p = 0; p < effects[e].params_count; p++) if (!strcmp(effects[e].params[p].code, argv[a])) effects[e].params[p].value = atoi(eq + 1);
    }
    framebuffer_t fbs = { w, h, calloc(w * h, sizeof(rgb_t)), 0 }, *fb = &fbs;
    sim_time_us = 1000000;
    if (effects[e].prepare) assert(effects[e].prepare(fb) == ESP_OK);
    FILE *out = fopen(argv[8], "wb");
    for (int f = 0; f < warm + step * frames; f++, sim_time_us += 1000000 / fps) {
        assert(effects[e].run(fb) == ESP_OK);
        if (f >= warm && (f - warm) % step == step - 1) fwrite(fb->data, sizeof(rgb_t), w * h, out);
    }
    fclose(out);
    return 0;
}
