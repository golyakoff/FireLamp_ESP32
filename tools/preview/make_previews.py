#!/usr/bin/env python3
"""
Effect previews: runs every effect of the firmware on a PC and saves a short slow GIF of each
to preview/<effect code>.gif, plus preview/README.md with all of them.

Needs gcc, ld, Pillow (pip install pillow) and the managed components of the firmware
(run `idf.py build` once). Run from anywhere: python3 tools/preview/make_previews.py
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
BUILD = os.path.join(HERE, 'build')
OUT = os.path.join(ROOT, 'preview')
MAIN = os.path.join(ROOT, 'main')
MC = os.path.join(ROOT, 'managed_components')
NSGIF = os.path.join(ROOT, 'components', 'libnsgif', 'libnsgif')

W = H = 16        # matrix size
FPS = 60          # default FPS of the firmware
FRAMES = 8        # frames in a GIF
DELAY_MS = 250    # GIF frame duration: slow on purpose
CELL, GAP = 10, 2 # pixels per LED and the gap between LEDs

# effect: (frames to skip before the capture, frames between captured frames, parameters)
DEFAULT = (240, 8, [])
TUNE = {
    'plasma_waves': (240, 3, []), 'dna': (240, 3, []), 'rays': (240, 3, []), 'crazybees': (240, 4, []),
    'metaballs': (240, 4, []), 'nuclear': (240, 4, []), 'octopus': (240, 4, []), 'wheel': (240, 4, []),
    'tunnel': (240, 6, []), 'gif': (30, 15, []),
}
# effects with rare events: move the capture until one of the middle frames has a channel this bright
EVENT = {'storm': 200}

# LEDs are linear, a screen is not: brighten the dim colors like the eye sees them on the lamp
GAMMA = [round(255 * (v / 255) ** (1 / 2.2)) for v in range(256)]


def build():
    """Compile the effects with the real framebuffer functions and the effect list of effect.c"""
    os.makedirs(BUILD, exist_ok=True)

    src = open(os.path.join(MC, 'esp-idf-lib__framebuffer', 'framebuffer.c')).read()
    fb = src[src.index('static size_t xy('):src.index('esp_err_t fb_init(')] + \
        src[src.index('esp_err_t fb_set_pixel_rgb('):src.index('esp_err_t fb_begin(')]
    with open(os.path.join(BUILD, 'fb.c'), 'w') as f:
        f.write('#include <stdlib.h>\n#include <string.h>\n#include "framebuffer.h"\n'
                '#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)\n' + fb)

    eff = open(os.path.join(MAIN, 'effect.c')).read()
    with open(os.path.join(BUILD, 'registry.c'), 'w') as f:
        f.write('#include "effect.h"\n')
        f.write(''.join(re.findall(r'#include "effects/[a-z_0-9]+\.h"\n', eff)))
        f.write(eff[eff.index('const effect_descriptor_t effects[]'):eff.index('const size_t effects_count')])
        f.write('const size_t effects_count = sizeof(effects) / sizeof(effect_descriptor_t);\n'
                'const char *TAG = "preview";\n')

    # embedded GIF files, with the symbol names the firmware uses
    objs = []
    gifs = os.path.join(ROOT, 'files', 'gifs')
    for g in sorted(os.listdir(gifs)):
        o = os.path.join(BUILD, g + '.o')
        subprocess.run(['ld', '-r', '-b', 'binary', '-z', 'noexecstack', g, '-o', o], cwd=gifs, check=True)
        objs.append(o)

    effects = sorted(os.path.join(MAIN, 'effects', f) for f in os.listdir(os.path.join(MAIN, 'effects'))
                     if f.endswith('.c'))
    subprocess.run(['gcc', '-O2', '-std=gnu17', '-w', '-D_GNU_SOURCE',
                    '-I' + os.path.join(HERE, 'stub'), '-I' + MAIN,
                    '-I' + os.path.join(MC, 'esp-idf-lib__lib8tion'), '-I' + os.path.join(MC, 'esp-idf-lib__color'),
                    '-I' + os.path.join(MC, 'esp-idf-lib__noise'), '-I' + os.path.join(NSGIF, 'include'),
                    '-o', os.path.join(BUILD, 'sim'), os.path.join(HERE, 'sim.c'),
                    os.path.join(BUILD, 'registry.c'), os.path.join(BUILD, 'fb.c'),
                    os.path.join(MAIN, 'palettes.c'), os.path.join(MAIN, 'utils.c'),
                    os.path.join(MC, 'esp-idf-lib__lib8tion', 'lib8tion.c'),
                    os.path.join(MC, 'esp-idf-lib__color', 'color.c'),
                    os.path.join(MC, 'esp-idf-lib__noise', 'noise.c')]
                   + effects
                   + [os.path.join(NSGIF, 'src', f) for f in sorted(os.listdir(os.path.join(NSGIF, 'src'))) if f.endswith('.c')]
                   + objs + ['-lm'], check=True)


def sim(*args):
    return subprocess.run([os.path.join(BUILD, 'sim')] + [str(a) for a in args],
                          check=True, capture_output=True, text=True).stdout


def capture(code):
    warm, step, params = TUNE.get(code, DEFAULT)
    raw = os.path.join(BUILD, code + '.raw')
    fs = W * H * 3
    while True:
        sim(code, W, H, FPS, warm, step, FRAMES, raw, *params)
        with open(raw, 'rb') as f:
            data = f.read()
        os.remove(raw)
        if code not in EVENT or any(max(data[k * fs:(k + 1) * fs]) >= EVENT[code] for k in range(2, 5)):
            return [data[k * fs:(k + 1) * fs] for k in range(FRAMES)]
        warm += step


def draw(frame):
    # imported here: the build and the simulator are used without Pillow (effect_peaks.py)
    from PIL import Image, ImageDraw

    img = Image.new('RGB', (W * CELL, H * CELL), (12, 12, 12))
    d = ImageDraw.Draw(img)
    for y in range(H):
        for x in range(W):
            i = (y * W + x) * 3
            c = tuple(GAMMA[v] for v in frame[i:i + 3])
            if c == (0, 0, 0):
                c = (20, 20, 20)  # unlit LED
            px, py = x * CELL, (H - 1 - y) * CELL  # y = 0 is the bottom line
            d.rounded_rectangle([px + GAP // 2, py + GAP // 2, px + CELL - GAP // 2 - 1, py + CELL - GAP // 2 - 1],
                                2, fill=c)
    return img


def main():
    build()
    os.makedirs(OUT, exist_ok=True)
    effects = [line.split('\t') for line in sim('--list').splitlines()]
    for code, name in effects:
        imgs = [draw(f) for f in capture(code)]
        imgs[0].save(os.path.join(OUT, code + '.gif'), save_all=True, append_images=imgs[1:],
                     duration=DELAY_MS, loop=0, optimize=True)
        print(code, file=sys.stderr)

    with open(os.path.join(OUT, 'README.md'), 'w') as f:
        f.write('# Effect previews\n\n'
                f'{W}x{H} matrix, default parameters, {FRAMES} frames of {DELAY_MS} ms (slower than on the lamp).\n'
                'Generated by `tools/preview/make_previews.py`; file names are the effect codes.\n\n'
                '| Effect | Code | Preview |\n|---|---|---|\n')
        for code, name in effects:
            f.write(f'| {name} | `{code}` | ![{name}]({code}.gif) |\n')
    print(len(effects), 'previews in', OUT)


if __name__ == '__main__':
    main()
