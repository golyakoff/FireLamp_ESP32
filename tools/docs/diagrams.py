#!/usr/bin/env python3
"""
Wiring and matrix orientation diagrams for the README, as plain SVG (no dependencies).

    python3 tools/docs/diagrams.py        # writes docs/images/*.svg (English) and *.ru.svg

The pins come from the reference pinout (main/Kconfig.projbuild defaults); the orientation table
follows build_led_map() in main/surface.c. The pictures have their own light background, so they
read the same in the light and the dark GitHub theme.
"""

import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, '..', '..', 'docs', 'images'))

FONT = "font-family=\"'Segoe UI', Helvetica, Arial, sans-serif\""
C_5V, C_GND, C_3V3, C_DATA, C_I2C = '#d32f2f', '#37474f', '#ef8f00', '#2e7d32', '#1565c0'
INK, MUTED, CARD, BOARD = '#263238', '#607d8b', '#ffffff', '#1b5e8f'

# the Russian labels (README.md is Russian, README.en.md English), with the app's own names
RU = {
    'FireLamp on {}: one button, LED matrix, optional RTC / EEPROM': 'FireLamp на {}: кнопка, матрица, RTC / EEPROM по желанию',
    '5 V power supply': 'Блок питания 5 В',
    '2 A for 8x8, 4 A+ for 16x16': '2 А для 8x8, от 4 А для 16x16',
    'BOOT button = Reset ({})': 'кнопка BOOT = сброс ({})',
    'WS2812B / SK6812 matrix': 'Матрица WS2812B / SK6812',
    '8x8 ... 32x32, any corner and wiring': '8x8 ... 32x32, любой угол и раскладка',
    'short wire': 'короткий провод',
    'button or TTP223 OUT': 'кнопка или выход TTP223',
    'pull-down inside {}; the pin can be changed in the app': 'подтяжка к GND внутри {}; пин меняется в приложении',
    'DS3231 RTC / AT24Cxx EEPROM': 'Часы DS3231 / EEPROM AT24Cxx',
    'optional, found automatically': 'по желанию, находятся сами',
    'VCC → 3V3, GND → GND': 'VCC → 3V3, GND → GND',
    'SDA, SCL: 4.7 kΩ pull-ups (most modules have them)': 'на SDA, SCL подтяжки 4,7 кОм (на модулях обычно есть)',
    '+5 V': '+5 В', '3.3 V': '3,3 В', 'LED data': 'данные матрицы', 'I2C (optional)': 'I2C (по желанию)',
    'One common GND for the supply, the board and the matrix. Power the matrix from the supply, not from the board.':
        'Общий GND у блока питания, платы и матрицы. Матрицу питать от блока, а не с платы.',
    'Matrix orientation: where the first LED (DIN) is and where the strip goes':
        'Ориентация матрицы: где первый светодиод (DIN) и куда идёт лента',
    'In the app: System → LED matrix, Rotation and Mirror horizontally.':
        'В приложении: «Система» → «Светодиодная матрица», «Поворот» и «Отразить по горизонтали».',
    'Rotation {}°': 'Поворот {}°', 'Mirror {}': 'отражение {}',
    'on': 'вкл', 'off': 'выкл',
    'bottom-left': 'внизу слева', 'bottom-right': 'внизу справа', 'top-left': 'вверху слева', 'top-right': 'вверху справа',
    'rows': 'ряды', 'columns': 'столбцы',
    'right': 'вправо', 'left': 'влево', 'up': 'вверх', 'down': 'вниз',
}
LANG = 'en'


def tr(s, *args):
    s = RU.get(s, s) if LANG == 'ru' else s
    return s.format(*args) if args else s


class Svg:
    def __init__(self, w, h):
        self.w, self.h, self.items = w, h, []

    def add(self, s):
        self.items.append(s)

    def rect(self, x, y, w, h, fill='none', stroke=INK, sw=1.5, rx=6, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ''
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')

    def text(self, x, y, s, size=13, fill=INK, anchor='start', weight='normal'):
        s = s.replace('&', '&amp;').replace('<', '&lt;')
        self.add(f'<text x="{x}" y="{y}" {FONT} font-size="{size}" fill="{fill}" text-anchor="{anchor}" font-weight="{weight}">{s}</text>')

    def wire(self, pts, color, dash=None, halo=False):
        p = ' '.join(f'{x},{y}' for x, y in pts)
        d = f' stroke-dasharray="{dash}"' if dash else ''
        if halo:  # a white gap where the wire crosses another one
            self.add(f'<polyline points="{p}" fill="none" stroke="{CARD}" stroke-width="9"/>')
        self.add(f'<polyline points="{p}" fill="none" stroke="{color}" stroke-width="3" stroke-linejoin="round"{d}/>')

    def dot(self, x, y, color):
        self.add(f'<circle cx="{x}" cy="{y}" r="4.5" fill="{color}"/>')

    def pin(self, x, y, label, side, color=INK):
        """A pad on the board edge with its label inside the board"""
        self.add(f'<circle cx="{x}" cy="{y}" r="5" fill="#ffd54f" stroke="{INK}" stroke-width="1.2"/>')
        dx = {'left': 12, 'right': -12}.get(side, 0)
        anchor = {'left': 'start', 'right': 'end'}.get(side, 'middle')
        dy = {'bottom': -12}.get(side, 4)
        self.text(x + dx, y + dy, label, 12, '#ffffff', anchor, 'bold')

    def resistor(self, x1, x2, y, label):
        m = (x1 + x2) / 2
        self.wire([(x1, y), (m - 22, y)], C_DATA)
        self.wire([(m + 22, y), (x2, y)], C_DATA)
        self.rect(m - 22, y - 8, 44, 16, fill='#fff8e1', stroke=INK, sw=1.5, rx=3)
        self.text(m, y - 14, label, 12, INK, 'middle')

    def capacitor(self, x1, x2, y, label):
        m = (x1 + x2) / 2
        self.wire([(x1, y), (m - 5, y)], C_5V)
        self.wire([(m + 5, y), (x2, y)], C_GND)
        self.add(f'<line x1="{m - 5}" y1="{y - 12}" x2="{m - 5}" y2="{y + 12}" stroke="{INK}" stroke-width="3"/>')
        self.add(f'<line x1="{m + 5}" y1="{y - 12}" x2="{m + 5}" y2="{y + 12}" stroke="{INK}" stroke-width="3"/>')
        self.text(m - 12, y - 8, '+', 13, C_5V, 'end', 'bold')
        self.text(m, y + 28, label, 12, INK, 'middle')

    def button(self, x1, x2, y, label):
        m = (x1 + x2) / 2
        self.wire([(x1, y), (m - 18, y)], C_3V3)
        self.wire([(m + 18, y), (x2, y)], C_3V3)
        for x in (m - 18, m + 18):
            self.add(f'<circle cx="{x}" cy="{y}" r="3.5" fill="{CARD}" stroke="{INK}" stroke-width="2"/>')
        self.add(f'<line x1="{m - 22}" y1="{y - 12}" x2="{m + 22}" y2="{y - 12}" stroke="{INK}" stroke-width="2.5"/>')
        self.add(f'<line x1="{m}" y1="{y - 12}" x2="{m}" y2="{y - 22}" stroke="{INK}" stroke-width="2.5"/>')
        self.add(f'<line x1="{m - 8}" y1="{y - 22}" x2="{m + 8}" y2="{y - 22}" stroke="{INK}" stroke-width="2.5"/>')
        self.text(m, y + 22, label, 12, INK, 'middle')

    def save(self, name):
        os.makedirs(OUT, exist_ok=True)
        body = '\n  '.join(self.items)
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {self.w} {self.h}" width="{self.w}" height="{self.h}">\n'
               f'  <rect width="{self.w}" height="{self.h}" rx="14" fill="#f5f7f8"/>\n  {body}\n</svg>\n')
        with open(os.path.join(OUT, name), 'w', encoding='utf-8', newline='\n') as f:
            f.write(svg)
        print('wrote', os.path.join(OUT, name))


def wiring(name, chip, board, module, pins):
    s = Svg(900, 640)
    s.text(30, 38, tr('FireLamp on {}: one button, LED matrix, optional RTC / EEPROM', chip), 18, INK, 'start', 'bold')

    # power supply and the 5 V / GND buses along the top
    s.rect(30, 70, 150, 92, fill=CARD)
    s.text(105, 98, tr('5 V power supply'), 13, INK, 'middle', 'bold')
    s.text(105, 116, tr('2 A for 8x8, 4 A+ for 16x16'), 11, MUTED, 'middle')
    s.text(170, 136, '+5V', 12, C_5V, 'end', 'bold')
    s.text(170, 156, 'GND', 12, C_GND, 'end', 'bold')
    s.wire([(180, 132), (790, 132)], C_5V)
    s.wire([(180, 152), (790, 152)], C_GND)

    # the board
    bx, by, bw, bh = 250, 210, 230, 250
    s.rect(bx, by, bw, bh, fill=BOARD, stroke='#0d3c61', sw=2, rx=10)
    s.text(bx + bw / 2, by + 104, board, 15, '#ffffff', 'middle', 'bold')
    s.text(bx + bw / 2, by + 122, module, 11, '#cfe3f3', 'middle')
    s.text(bx + bw / 2, by + 142, tr('BOOT button = Reset ({})', pins['boot']), 11, '#cfe3f3', 'middle')
    s.pin(bx, by + 30, '5V / VIN', 'left')
    s.pin(bx, by + 60, 'GND', 'left')
    s.pin(bx + bw, by + 40, pins['data'], 'right')
    s.pin(bx + bw, by + 180, pins['sda'] + ' SDA', 'right')
    s.pin(bx + bw, by + 210, pins['scl'] + ' SCL', 'right')
    s.pin(bx + 50, by + bh, '3V3', 'bottom')
    s.pin(bx + 150, by + bh, pins['button'], 'bottom')

    # board power from the buses
    s.wire([(bx, by + 60), (200, by + 60), (200, 152)], C_GND)
    s.dot(200, 152, C_GND)
    s.wire([(bx, by + 30), (215, by + 30), (215, 132)], C_5V, halo=True)  # jumps over GND
    s.dot(215, 132, C_5V)

    # the matrix
    mx, my, ms = 640, 210, 170
    s.rect(mx, my, ms, ms, fill='#212121', stroke=INK, sw=2, rx=6)
    for i in range(8):
        for j in range(8):
            colors = ['#ff6f00', '#ff8f00', '#ffa000', '#ffb300', '#ffca28', '#ffd54f', '#ffe082', '#ffecb3']
            c = colors[min(7, (7 - j) + (i % 3) - 1)] if j > 2 else '#3a3a3a'
            s.add(f'<circle cx="{mx + 20 + i * 18.5}" cy="{my + 20 + j * 18.5}" r="6" fill="{c}"/>')
    s.text(mx + ms / 2, my + ms + 22, tr('WS2812B / SK6812 matrix'), 13, INK, 'middle', 'bold')
    s.text(mx + ms / 2, my + ms + 40, tr('8x8 ... 32x32, any corner and wiring'), 11, MUTED, 'middle')
    s.pin(mx, my + 40, 'DIN', 'left')
    s.add(f'<circle cx="{mx + 50}" cy="{my}" r="5" fill="#ffd54f" stroke="{INK}" stroke-width="1.2"/>')
    s.add(f'<circle cx="{mx + 120}" cy="{my}" r="5" fill="#ffd54f" stroke="{INK}" stroke-width="1.2"/>')
    s.text(mx + 50, my + 20, '+5V', 11, '#ffffff', 'middle', 'bold')
    s.text(mx + 120, my + 20, 'GND', 11, '#ffffff', 'middle', 'bold')
    s.wire([(mx + 50, my), (mx + 50, 132)], C_5V, halo=True)  # jumps over GND
    s.dot(mx + 50, 132, C_5V)
    s.wire([(mx + 120, my), (mx + 120, 152)], C_GND)
    s.dot(mx + 120, 152, C_GND)
    s.capacitor(mx + 50, mx + 120, 178, '1000 µF')

    # data line with the series resistor
    s.resistor(bx + bw, mx, by + 40, '330 Ω')
    s.text((bx + bw + mx) / 2, by + 68, tr('short wire'), 11, MUTED, 'middle')

    # button between 3V3 and the button pin
    yb = by + bh + 50
    s.wire([(bx + 50, by + bh), (bx + 50, yb)], C_3V3)
    s.wire([(bx + 150, by + bh), (bx + 150, yb)], C_3V3)
    s.button(bx + 50, bx + 150, yb, tr('button or TTP223 OUT'))
    s.text(bx + 100, yb + 40, tr('pull-down inside {}; the pin can be changed in the app', pins['button']), 11, MUTED, 'middle')

    # optional I2C module
    ix, iy = 560, 455
    s.rect(ix, iy, 300, 88, fill=CARD, stroke=C_I2C, sw=1.5, dash='6 4')
    s.text(ix + 150, iy + 24, tr('DS3231 RTC / AT24Cxx EEPROM'), 13, INK, 'middle', 'bold')
    s.text(ix + 150, iy + 42, tr('optional, found automatically'), 11, MUTED, 'middle')
    s.text(ix + 150, iy + 62, tr('VCC → 3V3, GND → GND'), 11, MUTED, 'middle')
    s.text(ix + 150, iy + 78, tr('SDA, SCL: 4.7 kΩ pull-ups (most modules have them)'), 10, MUTED, 'middle')
    s.wire([(bx + bw, by + 180), (530, by + 180), (530, iy + 30), (ix, iy + 30)], C_I2C, dash='7 5')
    s.wire([(bx + bw, by + 210), (515, by + 210), (515, iy + 55), (ix, iy + 55)], C_I2C, dash='7 5')

    # legend
    lx, ly = 30, 600
    for i, (c, t) in enumerate([(C_5V, tr('+5 V')), (C_GND, 'GND'), (C_3V3, tr('3.3 V')), (C_DATA, tr('LED data')), (C_I2C, tr('I2C (optional)'))]):
        x = lx + i * 135
        s.add(f'<line x1="{x}" y1="{ly}" x2="{x + 24}" y2="{ly}" stroke="{c}" stroke-width="4"/>')
        s.text(x + 30, ly + 4, t, 12, INK)
    s.text(30, 625, tr('One common GND for the supply, the board and the matrix. Power the matrix from the supply, not from the board.'), 11, MUTED)
    s.save(name)


# first LED corner, direction of the first row/column -> (rotation, mirror), see build_led_map()
ORIENT = [
    ('bottom-left', 'right', 0, False), ('bottom-right', 'left', 0, True),
    ('top-right', 'left', 2, False), ('top-left', 'right', 2, True),
    ('bottom-right', 'up', 1, False), ('top-right', 'down', 1, True),
    ('top-left', 'down', 3, False), ('bottom-left', 'up', 3, True),
]


def orientation(name):
    n, cell = 4, 26
    cw, ch = 205, 225
    s = Svg(40 + 4 * cw, 90 + 2 * ch)
    s.text(30, 38, tr('Matrix orientation: where the first LED (DIN) is and where the strip goes'), 18, INK, 'start', 'bold')
    s.text(30, 62, tr('In the app: System → LED matrix, Rotation and Mirror horizontally.'), 12, MUTED)
    for k, (corner, direction, rot, mirror) in enumerate(ORIENT):
        ox = 30 + (k % 4) * cw
        oy = 85 + (k // 4) * ch
        s.rect(ox, oy, cw - 15, ch - 15, fill=CARD, stroke='#cfd8dc', sw=1.2, rx=10)
        gx, gy = ox + (cw - 15 - n * cell) / 2, oy + 16
        # strip order on a serpentine 4x4 in this orientation
        cells = []
        right = corner.endswith('left')
        bottom = corner.startswith('bottom')
        for line in range(n):
            seq = list(range(n))
            if direction in ('right', 'left'):
                forward = (direction == 'right') ^ (line % 2 == 1)
                xs = seq if forward else seq[::-1]
                y = (n - 1 - line) if bottom else line
                cells += [(x, y) for x in xs]
            else:
                forward = (direction == 'down') ^ (line % 2 == 1)
                ys = seq if forward else seq[::-1]
                x = line if right else (n - 1 - line)
                cells += [(x, y) for y in ys]
        pts = [(gx + x * cell + cell / 2, gy + y * cell + cell / 2) for x, y in cells]
        for x in range(n):
            for y in range(n):
                s.add(f'<circle cx="{gx + x * cell + cell / 2}" cy="{gy + y * cell + cell / 2}" r="7" fill="#eceff1" stroke="#b0bec5"/>')
        s.add('<polyline points="{}" fill="none" stroke="{}" stroke-width="2.5" stroke-linejoin="round" opacity="0.85"/>'.format(
            ' '.join(f'{x},{y}' for x, y in pts), C_DATA))
        x0, y0 = pts[0]
        s.add(f'<circle cx="{x0}" cy="{y0}" r="8" fill="{C_5V}"/>')
        s.text(x0, y0 + 4, '1', 10, '#ffffff', 'middle', 'bold')
        x1, y1 = pts[-1]
        s.add(f'<circle cx="{x1}" cy="{y1}" r="4" fill="{C_DATA}"/>')
        ty = gy + n * cell + 22
        s.text(ox + (cw - 15) / 2, ty, f'{tr(corner)}, {tr("rows" if direction in ("right", "left") else "columns")} → {tr(direction)}', 11, MUTED, 'middle')
        s.text(ox + (cw - 15) / 2, ty + 22, tr('Rotation {}°', rot * 90), 13, INK, 'middle', 'bold')
        s.text(ox + (cw - 15) / 2, ty + 40, tr('Mirror {}', tr('on' if mirror else 'off')), 12, INK, 'middle')
    s.save(name)


def make_all(suffix):
    wiring(f'wiring-esp32{suffix}.svg', 'ESP32', 'ESP32 DevKit', 'ESP32-WROOM-32',
           {'data': 'GPIO5', 'button': 'GPIO25', 'sda': 'GPIO21', 'scl': 'GPIO22', 'boot': 'GPIO0'})
    wiring(f'wiring-esp32c3{suffix}.svg', 'ESP32-C3', 'ESP32-C3', 'SuperMini, DevKitM',
           {'data': 'GPIO5', 'button': 'GPIO4', 'sda': 'GPIO3', 'scl': 'GPIO10', 'boot': 'GPIO9'})
    orientation(f'matrix-orientation{suffix}.svg')


if __name__ == '__main__':
    make_all('')
    LANG = 'ru'
    make_all('.ru')
