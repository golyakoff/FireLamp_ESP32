#!/usr/bin/env python3
"""
Fit the built-in GIFs (files/gifs) to a small power supply.

Pixel art is drawn for screens with a light background; on the LEDs that background is most of the
current (the white one of heart2 is 2/3 of the matrix). For every GIF:
- the frames are composed the way the lamp shows them;
- the background (the area of the corner pixel colour connected to the border) becomes black;
- when the brightest frame is still above BUDGET of the full white matrix, all colours are dimmed
  in proportion;
- the GIF is written again as full frames without transparency, with the same delays.

A GIF that is already dark enough stays as it is. Run: python3 tools/gifs/fit_power.py [file.gif ...]
Needs Pillow.
"""
import os
import sys

from PIL import Image, ImageSequence

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GIFS = os.path.join(ROOT, 'files', 'gifs')
# brightest frame, part of the current of the full white matrix: the firmware caps the brightness
# so that the full white matrix takes the current limit, a GIF takes at most this part of it
BUDGET = 0.35
# a fitted GIF is a little above BUDGET after the rounding: it is not dimmed again on the next run
TOLERANCE = 0.005


def load(frame):
    """Part of the full white current the frame takes"""
    return sum(frame.tobytes()) / (frame.width * frame.height * 3 * 255)


def remove_background(frame):
    """Black instead of the area of the corner pixel colour connected to the border"""
    w, h = frame.size
    px = frame.load()
    bg = px[0, 0]
    if bg == (0, 0, 0):
        return frame
    stack = [(x, y) for x in range(w) for y in (0, h - 1)] + [(x, y) for y in range(h) for x in (0, w - 1)]
    seen = set()
    while stack:
        x, y = stack.pop()
        if (x, y) in seen or not (0 <= x < w and 0 <= y < h) or px[x, y] != bg:
            continue
        seen.add((x, y))
        px[x, y] = (0, 0, 0)
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return frame


def fit(path):
    src = Image.open(path)
    frames, delays = [], []
    for f in ImageSequence.Iterator(src):
        # composed frame on black: the transparent pixels are unlit LEDs on the lamp
        rgba = f.convert('RGBA')
        frame = Image.new('RGB', rgba.size, (0, 0, 0))
        frame.paste(rgba, mask=rgba.getchannel('A'))
        frames.append(frame)
        delays.append(f.info.get('duration', 100))
    loop = src.info.get('loop', 0)

    before = max(load(f) for f in frames)
    if before <= BUDGET + TOLERANCE:
        print(f'{os.path.basename(path):14} {before:4.0%}  kept')
        return

    frames = [remove_background(f) for f in frames]
    peak = max(load(f) for f in frames)
    if peak > BUDGET:
        k = BUDGET / peak
        frames = [f.point(lambda v: round(v * k)) for f in frames]
    after = max(load(f) for f in frames)

    # one palette for all the frames, of exactly their colours (no more than the original ones)
    strip = Image.new('RGB', (frames[0].width, frames[0].height * len(frames)))
    for i, f in enumerate(frames):
        strip.paste(f, (0, i * f.height))
    colours = len(strip.getcolors(256))
    palette = strip.quantize(colors=colours, method=Image.Quantize.MAXCOVERAGE, dither=Image.Dither.NONE)
    out = [f.quantize(palette=palette, dither=Image.Dither.NONE) for f in frames]
    out[0].save(path, save_all=True, append_images=out[1:], duration=delays, loop=loop,
                disposal=1, optimize=False)
    print(f'{os.path.basename(path):14} {before:4.0%} -> {after:4.0%}')


def main():
    paths = sys.argv[1:] or sorted(os.path.join(GIFS, g) for g in os.listdir(GIFS) if g.endswith('.gif'))
    for p in paths:
        fit(p)


if __name__ == '__main__':
    main()
