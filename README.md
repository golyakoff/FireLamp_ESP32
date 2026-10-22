# FireLamp ESP32

Firmware for an LED matrix "fire lamp" on ESP32 / ESP32-C3, controlled from a phone over
Bluetooth Low Energy. Based on [EvLamp](https://github.com/UncleRus/EvLamp) by Ruslan V. Uss
(~30 effects in pure C on ESP-IDF); BLE control follows the approach of
[Matrix_Clock_ESP32](https://github.com/golyakoff/Matrix_Clock_ESP32).

> **Status: work in progress.** The EvLamp core is ported to a single matrix and builds for both
> targets; settings storage (NVS or external EEPROM), buttons/touch, time (optional DS3231 RTC),
> schedules, demo mode, BLE control, the optional WiFi client, firmware update from the phone
> or from the internet, CI and releases are done. Not yet tested on real hardware.

## Hardware

- ESP32 (e.g. ESP32-WROOM-32D) or ESP32-C3 (e.g. ESP32-C3-FH4), 4 MB flash
- One WS2812B / SK6812 / WS2811 LED matrix, 16x16 recommended (up to 1024 LEDs)
- Controls: one button (or a TTP223 touch module), four buttons, or the ESP32 built-in touch pad
- Optional DS3231 RTC (keeps time while unpowered) and AT24C32..AT24C512 EEPROM for settings,
  on the I2C bus

One firmware per chip fits every lamp with the reference pinout:

| Function | ESP32 | ESP32-C3 |
|---|---|---|
| Matrix data | GPIO5 | GPIO5 |
| Main button (default, can be moved in the app) | GPIO25 | GPIO4 |
| Up / Down buttons | GPIO26 / GPIO27 | GPIO6 / GPIO7 |
| Reset button (active low) | GPIO0 (BOOT) | GPIO9 (BOOT) |
| Touch pad | T0 = GPIO4 | - |
| I2C SDA / SCL | GPIO21 / GPIO22 | GPIO3 / GPIO10 |

Reset is the board's BOOT button (active low). Everything else is detected or set at runtime:

- **DS3231** and **EEPROM** (address 0x50) are detected on the I2C bus at boot. With an EEPROM
  the settings are kept in it and survive `erase-flash`; an EEPROM added later takes over the
  settings from the internal flash.
- **Control elements** (one button / four buttons / ESP32 touch pad), the **buttons type**
  (TTP223 module or a button to +3.3 V, or a button to GND) and the **Main button pin**
  (ESP32: GPIO 4, 13, 14, 18, 19, 23, 25, 32, 33; ESP32-C3: GPIO 1, 4) - settings in the app
  (BLE `Hardware settings`, HTTP `/api/settings/hardware`).
- **Matrix**: size, LED type, color order, rotation, mirror, serpentine or progressive wiring,
  current limit - a setting in the app (BLE `LED settings`, HTTP `/api/settings/leds`).

A board with other pins needs its own build (menuconfig), see [Build variant](#build-variant).

LED output uses [espressif/led_strip](https://components.espressif.com/components/espressif/led_strip):
SPI with DMA by default on ESP32-C3 (its RMT has no DMA), RMT by default on ESP32.
The previous frame is transmitted while the next one is computed.

## Controls

One button / touch pad:

| Lamp | Gesture | Action |
|------|---------|--------|
| off  | click | turn on |
| off  | hold 10 s | reset all settings to defaults and reboot |
| on   | click | turn off |
| on   | double click | next effect |
| on   | triple click | previous effect |
| on   | hold | change brightness; direction alternates with every hold |
| any  | 5 clicks | open the BLE pairing window for 2 minutes (short blue blink) |

Four buttons (as in EvLamp): **Main** - click: next effect, 5 clicks: BLE pairing window,
hold 1.5 s: turn on/off; **Up/Down** - brightness, hold to repeat; **Reset** - hold 3 s:
reset all settings.

In the demo mode next/previous effect switch to the next included effect and the demo goes on.

## BLE control

The lamp is controlled from the phone app over Bluetooth Low Energy, see
[docs/BLE_PROTOCOL.md](docs/BLE_PROTOCOL.md): lamp state, effects and their parameters, time,
schedules, demo mode, LED and WiFi settings, firmware update. Notes for the app developer:
[docs/APP_INTEGRATION.md](docs/APP_INTEGRATION.md).

- Effects and parameters are identified by text codes (`fire`, `plasma_waves`, `speed`, ...),
  so app and firmware versions do not depend on the order of effects.
- Only paired phones can control the lamp. A new phone can pair during 2 minutes after power on
  or after 5 clicks of the main button; up to 4 phones are remembered.
- Firmware update from the phone: the app uploads the release `.bin`; the update is verified
  (optionally with SHA-256) and rolled back automatically if the new firmware does not start.

## Demo mode

Plays the included effects one after another: sequentially or in random order, each for a set
duration plus an optional random addition. Effects can be excluded. It is a mode, not an effect:
turned on and off via the lamp state (BLE or HTTP API) and resumed after reboot.

## WiFi (optional)

The lamp works without WiFi. When configured (from the app over BLE, or in menuconfig), it
connects to a home network as a client:

- **periodic**: every N hours (1..24) connects, syncs time by SNTP and disconnects;
- **always on**: stays connected; the [HTTP API](API.md) is available.

There is no access point mode: the network is configured over BLE.

## Firmware update

- **From the phone over BLE**: the app uploads the release `.bin`, no WiFi needed.
- **From the internet**: on every WiFi session the lamp reads the release manifest
  (`manifest.json` of the latest GitHub release, URL in menuconfig) and reports whether a newer
  version is available. It installs the update **only on request** from the app (or
  `POST /api/update`).

Both ways verify the image (and its SHA-256 when known) and roll back automatically if the new
firmware does not start. The manifest is generated by `tools/make_manifest.py`.

## Settings storage

Settings are kept in an external AT24Cxx EEPROM when one is found on the I2C bus at boot (it
survives `erase-flash` and module replacement), otherwise in the internal flash (NVS).

- Every record has a magic number, size and CRC. In the EEPROM each record has two slots that are
  written alternately, so a power loss during a write keeps the previous copy.
- Frequently changed values (brightness, current effect, effect parameters) are written
  15 seconds after the last change, and on reboot.
- Effect parameters are stored by effect name, so they survive firmware updates that add or
  reorder effects.

## Time and schedules

The lamp keeps UTC and a POSIX time zone string (daylight saving time is handled). Time comes from
the optional DS3231 RTC at boot, then from the phone (BLE), the internet (SNTP) or the HTTP API;
when the RTC is present it is updated by these sources and corrects the system clock hourly.
Without RTC and before the time is set the lamp works normally, only schedules wait.

Schedules (see [API.md](API.md#get-schedule)):

- **Power on / power off** at a given time on selected days of week
- **Dawn alarm**: turns the lamp on with a chosen effect and raises brightness over 1..120
  minutes, reaching the full brightness at the alarm time; manual control stops it
- **Hourly brightness**: 24 values, applied when the lamp is on and at every hour change

Automatic brightness changes (dawn ramp, hourly) are not written to the settings storage.

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

## Releases

Every push builds the firmware for both chips and runs the host tests
([.github/workflows/build.yml](.github/workflows/build.yml)).

To make a release, bump `version.txt`, add the notes to [CHANGELOG.md](CHANGELOG.md), commit and
push a tag `v<version>` (it must match `version.txt`). The release workflow builds both chips and
publishes per chip: separate flash images, a merged image, a Windows `.bat` and a zip, plus
`manifest.json` for the internet update ([tools/make_release.sh](tools/make_release.sh) also
writes the release page text). The release can be checked locally:

```bash
tools/make_release.sh v$(cat version.txt)   # after building both targets, needs esptool >= 5
```

### Build variant

Releases contain one build per chip with the reference pinout, build variant `default`. A lamp
with other pins needs its own build: set the pins and a variant name of your own in menuconfig
(`*** FireLamp configuration ***` → `Build variant name`). The internet update installs only a
build of the same chip and variant, so official releases are never installed on such a lamp.
The variant is also reported over BLE (Device info).

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

The JSON API of EvLamp is kept (the web UI is removed) and available in the "always on" WiFi
mode, see [API.md](API.md).
LED settings now describe a single matrix: `width`, `height`, `type`
(0 = WS2812, 1 = SK6812, 2 = WS2811), `color_order` (0 = GRB, 1 = RGB), `rotation`, `mirror`,
`current_limit`.

## License

MIT, see [LICENSE](LICENSE). Bundled [libnsgif](components/libnsgif/libnsgif) is MIT licensed
(see its `COPYING`).
