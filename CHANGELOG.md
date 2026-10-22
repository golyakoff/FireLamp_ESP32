Release 0.2.0

- [x] New effect "Starfall" (`starfall`, from GyverLamp gunner47_v2 by PalPalych): comets with tails flying diagonally; not colored, it is a snowstorm
- [x] New effect "Thunderstorm" (`storm`, "Гроза в банке" from GyverLamp gunner47_v2): rain with splashes, clouds and lightning; the lightning flickers and lights up the clouds

Release 0.1.6

- [x] BLE: "Service Changed" is indicated to bonded phones after every boot, so a phone sees characteristics added by a firmware update (Android caches the attribute table of a bonded lamp)

Release 0.1.5

- [x] BLE: WiFi network scan for choosing the SSID in the app (command `0x08`, characteristic `0x0014` WiFi networks, read page by page), works in any WiFi mode
- [x] Periodic WiFi mode: a check / install request from the app is answered with an error when the lamp cannot connect (before, no answer)
- [x] Internet update: free heap and connection errors are logged

Release 0.1.4

- [x] Firmware update no longer turns the demo mode off: it goes on after the update (before, the lamp rebooted with the demo mode off)
- [x] A failed or aborted firmware update turns the lamp back on if it was on (before, it stayed dark)

Release 0.1.3

- [x] GIF effect: the image is scaled to fit the matrix keeping the aspect ratio (averaging pixels when scaled down) and centered; before, on matrices smaller than 16x16 only a few dots were shown
- [x] Settings are saved 15 s after the last change instead of 3 s

Release 0.1.2

- [x] BLE: a disconnect aborts only a firmware upload over BLE; an update from the internet started by the app goes on without the phone
- [x] docs/APP_INTEGRATION.md: notes for the app developer

Release 0.1.1

- [x] BLE: a connection with no requests and no enabled notifications for 5 minutes is dropped, so the lamp advertises again (Android keeps the link to a just bonded lamp open by itself)
- [x] docs/BLE_PROTOCOL.md: the app should list bonded lamps besides the scan results

Release 0.1.0

First version: EvLamp ported to a BLE controlled lamp for ESP32 and ESP32-C3.

- [x] EvLamp core on ESP-IDF 5.5: ~30 effects, single LED matrix (rotation, mirror, color order), LED output via `espressif/led_strip` (SPI + DMA on ESP32-C3, RMT on ESP32), next frame computed while the previous one is sent; `tunnel` effect rewritten to integer math (ESP32-C3 has no FPU); optional effects benchmark at boot
- [x] Effects and their parameters identified by text codes (`fire`, `plasma_waves`, `speed`...) in BLE, HTTP API and settings storage
- [x] One firmware per chip: fixed reference pinout, the DS3231 RTC and the AT24Cxx EEPROM are detected on the I2C bus, the control elements, the buttons type (TTP223 / button to +3.3 V or to GND) and the matrix (size, type, orientation, serpentine/progressive wiring) are set from the app
- [x] Settings in the internal flash or in an external AT24Cxx EEPROM (settings move over when an EEPROM is added), with CRC, two alternating copies in EEPROM and deferred writes
- [x] Controls: one button / TTP223, four buttons, or the ESP32 touch pad
- [x] Time: DS3231 RTC if present, time from the phone or SNTP, POSIX time zones; schedules: power on/off, dawn alarm, hourly brightness
- [x] Demo mode: sequential or random effects, duration + random addition, excluded effects
- [x] BLE control (see docs/BLE_PROTOCOL.md), pairing window (2 minutes after power on or 5 clicks), up to 4 phones
- [x] Optional WiFi client: periodic (time sync and update check) or always on (HTTP API)
- [x] Firmware update from the phone over BLE or from the internet (GitHub releases, installed on request), image and SHA-256 verification, automatic rollback
- [x] CI: build for both chips and host tests on every push, release assets and update manifest on tags
