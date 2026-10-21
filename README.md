# FireLamp ESP32

Firmware for an LED matrix "fire lamp" on ESP32 / ESP32-C3, controlled from a phone over
Bluetooth Low Energy. Based on [EvLamp](https://github.com/UncleRus/EvLamp) by Ruslan V. Uss
(~30 effects in pure C on ESP-IDF); BLE control follows the approach of
[Matrix_Clock_ESP32](https://github.com/golyakoff/Matrix_Clock_ESP32).

> **Status: work in progress.** The EvLamp core is ported to a single matrix and builds for both
> targets; settings storage (NVS or external EEPROM) and buttons/touch are done. BLE control,
> schedules, optional RTC and internet OTA are not implemented yet.

## Hardware

- ESP32 (e.g. ESP32-WROOM-32D) or ESP32-C3 (e.g. ESP32-C3-FH4), 4 MB flash
- One WS2812B / SK6812 / WS2811 LED matrix, 16x16 recommended (up to 1024 LEDs), connected
  like a snake starting from the bottom left corner
- Controls, selected in menuconfig: one button (or a TTP223 touch module), four buttons,
  or the ESP32 built-in touch pad (ESP32 only, ESP32-C3 has no touch sensor)
- Optional AT24C32..AT24C512 I2C EEPROM for settings

LED output uses [espressif/led_strip](https://components.espressif.com/components/espressif/led_strip):
SPI with DMA by default on ESP32-C3 (its RMT has no DMA), RMT by default on ESP32.
The previous frame is transmitted while the next one is computed.

## Controls

One button / touch pad:

| Lamp | Gesture | Action |
|------|---------|--------|
| off  | click | turn on |
| off  | hold 10 s | reset all settings to defaults and reboot |
| on   | click | next effect |
| on   | double click | turn off |
| on   | triple click | previous effect |
| on   | hold | change brightness; direction alternates with every hold |

Four buttons (as in EvLamp): **Main** - click: next effect, hold 1.5 s: turn on/off;
**Up/Down** - brightness, hold to repeat; **Reset** - hold 3 s: reset all settings.

## Settings storage

Settings are kept either in the internal flash (NVS) or in an external AT24Cxx EEPROM
(menuconfig → `Settings storage`), which survives `erase-flash` and module replacement.
If the EEPROM does not respond at boot, the lamp logs a warning and uses NVS.

- Every record has a magic number, size and CRC. In the EEPROM each record has two slots that are
  written alternately, so a power loss during a write keeps the previous copy.
- Frequently changed values (brightness, current effect, effect parameters) are written
  3 seconds after the last change, and on reboot.
- Effect parameters are stored by effect name, so they survive firmware updates that add or
  reorder effects.

## Building

Requires ESP-IDF v5.5.x.

```bash
. ~/esp/esp-idf-v5.5.6/export.sh

# ESP32
idf.py -B build-esp32 -DSDKCONFIG=build-esp32/sdkconfig -DIDF_TARGET=esp32 build
# ESP32-C3
idf.py -B build-esp32c3 -DSDKCONFIG=build-esp32c3/sdkconfig -DIDF_TARGET=esp32c3 build

# configure / flash / monitor (same -B/-DSDKCONFIG arguments)
idf.py -B build-esp32c3 -DSDKCONFIG=build-esp32c3/sdkconfig menuconfig
idf.py -B build-esp32c3 -DSDKCONFIG=build-esp32c3/sdkconfig -p /dev/ttyACM0 flash monitor
```

Each target keeps its own build directory and sdkconfig, so both can be built side by side.
Project options are in `menuconfig` → `*** FireLamp configuration ***`.

### Effects benchmark

Enable `Benchmark all effects at boot` in menuconfig to have the firmware run every effect for
100 frames at boot and log the average/maximum computation time per frame. Useful to check the
CPU headroom on ESP32-C3 (single core, no FPU).

### Host tests

Hardware independent logic (settings storage with simulated EEPROM and power loss, button
gestures) is covered by unit tests that run on the PC:

```bash
make -C test/host
```

## Flash layout (4 MB)

| Partition | Offset     | Size      |
|-----------|------------|-----------|
| nvs       | `0x9000`   | 20 KB     |
| otadata   | `0xe000`   | 8 KB      |
| phy_init  | `0x10000`  | 4 KB      |
| ota_0     | `0x20000`  | 1920 KB   |
| ota_1     | `0x200000` | 1920 KB   |

The bootloader is written at `0x1000` on ESP32 and at `0x0` on ESP32-C3.

## HTTP API

The JSON API of EvLamp is kept (the web UI is removed), see [API.md](API.md).
LED settings now describe a single matrix: `width`, `height`, `type`
(0 = WS2812, 1 = SK6812, 2 = WS2811), `color_order` (0 = GRB, 1 = RGB), `rotation`, `mirror`,
`current_limit`.

## License

MIT, see [LICENSE](LICENSE). Bundled [libnsgif](components/libnsgif/libnsgif) is MIT licensed
(see its `COPYING`).
