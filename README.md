# FireLamp ESP32

Firmware for an LED matrix "fire lamp" on ESP32 / ESP32-C3, controlled from a phone over
Bluetooth Low Energy. Based on [EvLamp](https://github.com/UncleRus/EvLamp) by Ruslan V. Uss
(~30 effects in pure C on ESP-IDF); BLE control follows the approach of
[Matrix_Clock_ESP32](https://github.com/golyakoff/Matrix_Clock_ESP32).

> **Status: work in progress.** The EvLamp core is ported to a single matrix and builds for both
> targets. BLE control, schedules, optional RTC/EEPROM and internet OTA are not implemented yet.

## Hardware

- ESP32 (e.g. ESP32-WROOM-32D) or ESP32-C3 (e.g. ESP32-C3-FH4), 4 MB flash
- One WS2812B / SK6812 / WS2811 LED matrix, 16x16 recommended (up to 1024 LEDs), connected
  like a snake starting from the bottom left corner
- Buttons (see `idf.py menuconfig`)

LED output uses [espressif/led_strip](https://components.espressif.com/components/espressif/led_strip):
SPI with DMA by default on ESP32-C3 (its RMT has no DMA), RMT by default on ESP32.
The previous frame is transmitted while the next one is computed.

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
