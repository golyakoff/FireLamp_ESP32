# FireLamp: guide for the app developer

What the Android app needs to know to control FireLamp. The byte-level contract is
[BLE_PROTOCOL.md](BLE_PROTOCOL.md); this document explains how to use it: the order of
operations, what to cache, and how FireLamp differs from Matrix Clock (the same app family).

Firmware state: v0.1.0, protocol version 1. The firmware has not been tested on real hardware yet;
if the lamp behaves differently from these documents, the documents describe the intent.

## 1. Discovery and connection

1. Scan for the service UUID `F1A70000-B390-49C3-8770-A1DFC323D1AF` (it is in the advertising
   packet). The name (`FireLamp-XXXX` or a user-defined one) is in the scan response, so use
   active scanning to show it.
   **Also list the bonded lamps** from `BluetoothAdapter.getBondedDevices()`: a connected lamp
   does not advertise, and Android often keeps the link to a just bonded lamp open by itself
   (the lamp drops a link with no requests and no notifications after 5 minutes). `connectGatt()`
   to a bonded lamp works while the system holds the link.
2. Connect, then **request MTU 517** right away (`requestMtu(517)`). Everything works with a smaller
   MTU, except the OTA chunk size (`MTU - 3`) and long reads (Android does them automatically).
3. Discover services. All FireLamp characteristics are in the one service.
4. **Bond** if the phone is not bonded yet (see 2), then read [Device info](#3-first-reads).
5. Write the current time (see 5).
6. Enable notifications (CCCD) for:
   - `0x0002` Lamp state
   - `0x0004` Effect parameter
   - `0x000A` WiFi status
   - `0x0010` OTA control (status)
   - `0x0012` Update info

Only one phone can be connected at a time; the lamp advertises again after a disconnect.
Android permissions: `BLUETOOTH_SCAN` and `BLUETOOTH_CONNECT` (Android 12+), location for scanning
on older versions.

## 2. Pairing (bonding)

Unlike Matrix Clock, FireLamp accepts only **bonded** phones. Pairing is "Just Works" (no PIN): the
user only confirms the system dialog. New phones can pair only in the **pairing window**:

- 2 minutes after the lamp is powered on;
- 2 minutes after 5 clicks of the main button (the lamp blinks blue).

Recommended flow:

1. After connecting, if `device.bondState != BOND_BONDED`, call `device.createBond()` and wait for
   `ACTION_BOND_STATE_CHANGED` → `BOND_BONDED`. (Without it, the first read fails with
   `GATT_INSUFFICIENT_ENCRYPTION` (0x0F) and Android starts bonding by itself, but the explicit way
   is easier to handle.)
2. If bonding fails, or the lamp disconnects right after bonding, the window was closed: the lamp
   drops keys of phones paired outside the window. Show a hint "Click the lamp button 5 times and
   try again". The phone may keep a stale bond then: remove it (`removeBond()` via reflection, or
   ask the user to "Forget" the lamp in the Bluetooth settings) before retrying.
3. A phone that does not finish pairing in 30 s is disconnected by the lamp.

The lamp remembers up to 4 phones; a 5th bond forgets the oldest one. Factory reset forgets all.

## 3. First reads

**Device info** (`0x0001`): protocol version, chip, flash size, features, matrix size, number of
effects, controls in use, firmware version, build date, build variant.

- Refuse to work (or work read-only) with a protocol version the app does not know.
- `features` bit 0 / 1: DS3231 RTC / EEPROM found on the lamp; bit 2 / 3: WiFi / internet update
  supported (always set in v0.1.0).
- Chip `0` = ESP32, `1` = ESP32-C3 and the **build variant** (`default` for official releases)
  select the OTA file, see 9.

Then read **Lamp state** (`0x0002`) and the effect list (see 4).

## 4. Effects and parameters: codes, not numbers

Everything about effects uses **text codes** (up to 16 characters `a-z0-9_`): `fire`,
`plasma_waves`, `speed`, `palette`... Codes never change between firmware versions; the order of
the list may. Never store or send effect positions.

Reading the list (`0x0003` Effect info):

1. Write `00 00` (select position 0), read: the answer has the total count.
2. For positions `1 .. count-1`: write `00 <position>`, read.

Each answer: position, count, effect code, English name, flags (bit 0 = included in the demo
mode), and parameters (code, English name, type range/bool, min, max, default, current value).

- **Cache** the descriptions by firmware version (from Device info): they do not change for the
  same firmware, except current values and the demo flag. With a cache, read only Effect
  parameter (`0x0004`, values of the current effect) on connection.
- **Localize** by codes: keep your own names/icons for known codes, fall back to the English name
  for codes the app does not know (newer firmware).
- To refresh one effect: write `01 <str8 code>`, read.

Setting a parameter: write `0x0004` with `str8 effect code, str8 parameter code, u8 value`. When
the value actually changes, the lamp notifies the same triple (also for changes made elsewhere,
e.g. the HTTP API). Writing the current value again gives no notification: rely on the write
response for success.

Current effects (generated from firmware v0.1.0 sources, for localization):

| Effect code | English name | Parameters: code (English name) |
|---|---|---|
| `lamp` | Color lamp | `red` (Red), `green` (Green), `blue` (Blue) |
| `fire` | Fire | `palette` (Palette), `scale` (Scale) |
| `sparkles` | Sparkles | `sparkles` (Number of sparkles), `fadeout` (Fadeout speed) |
| `lava` | Lava | `scale` (Scale), `speed` (Speed), `mode` (Mode), `palette` (Palette) |
| `waterfall` | Waterfall | `mode` (Mode), `color` (Color), `cooling` (Cooling), `sparking` (Sparking) |
| `plasma_waves` | Plasma waves | `speed` (Speed) |
| `dna` | DNA helix | `rotation_speed` (Rotation speed), `spiral_size` (Spiral size), `white_border` (Add white border, bool) |
| `matrix` | Matrix | `density` (Density) |
| `crazybees` | Crazy bees | `bees` (Number of bees), `bee_hue` (Bee hue), `bee_saturation` (Bee saturation) |
| `rain` | Rain | `colored` (Colored, bool), `hue` (Hue), `density` (Density), `tail_length` (Tail length) |
| `rainbow` | Rainbow | `direction` (Direction), `scale` (Scale), `speed` (Speed) |
| `rays` | Rays | `speed` (Speed), `min_rays` (Min rays), `max_rays` (Max rays) |
| `metaballs` | Metaballs | `palette` (Palette), `balls` (Balls) |
| `water` | Water | `hue` (Hue), `saturation` (Saturation), `speed` (Speed) |
| `holiday` | Holiday lights | `mode` (Mode), `speed` (Speed) |
| `hoop` | Hoop | `speed` (Speed) |
| `twister` | Twister | `speed` (Speed), `black_border` (Add black border, bool), `iridescent` (Iridescent colors, bool) |
| `flower` | Flower | `speed` (Speed), `diameter` (Diameter) |
| `polar_lights` | Polar lights | `speed` (Speed), `scale` (Scale), `palette` (Palette), `height` (Height adjustment) |
| `gif` | Gif image | `file` (File) |
| `relax` | Relax | `speed` (Speed), `start_hue` (Start hue), `saturation` (Saturation), `pixels` (Pixels to update) |
| `wheel` | Radiant wheel | `rotation_speed` (Rotation speed), `hue_1` (Hue 1), `saturation_1` (Saturation 1), `hue_2` (Hue 2), `saturation_2` (Saturation 2), `rays` (Number of rays) |
| `peacock` | Peacock tail | `speed` (Speed), `scale` (Scale), `saturation` (Saturation) |
| `diagonals` | Diagonal movements | `speed` (Speed) |
| `whirls` | Whirls | `count` (Count), `scale` (Scale), `palette` (Palette), `fadeout` (Fadeout), `time_scale` (Time scale) |
| `sun` | Sun radiation | `size` (Size), `palette` (Palette) |
| `spaceships` | Flight of spaceships | `ships` (Number of ships), `turn_time` (Direction change time) |
| `fireflies` | Fireflies | `fireflies` (Number of fireflies), `chaotic` (Chaotic movement, bool), `speed` (Speed), `update_time` (Update time) |
| `tunnel` | Tunnel | `speed` (Speed) |
| `dunes` | Colored dunes | `speed` (Speed), `palette` (Palette) |
| `octopus` | Octopus | `speed` (Speed), `legs` (Legs), `clockwise` (Clockwise, bool) |
| `nuclear` | Nuclear Noise | `speed` (Speed) |

## 5. Time and time zone

- On **every connection** write the phone's current time to `0x0005`: u32 **UTC** seconds since
  1970 (`System.currentTimeMillis() / 1000`). Matrix Clock took local time; FireLamp takes UTC.
- The lamp converts to local time with a **POSIX TZ string** in `0x0006`, e.g. `MSK-3`,
  `CET-1CEST,M3.5.0,M10.5.0/3`, so daylight saving time works even when the phone is away.
  Android gives an IANA id (`ZoneId.systemDefault()`, e.g. `Europe/Berlin`). Map it to a POSIX
  string: a table of common zones is enough, or build the string from
  `ZoneId.getRules()` (standard offset, DST offset and `getTransitionRules()`). Note the POSIX sign
  is inverted: UTC+3 is `XXX-3`.
- Write the time zone when it differs from the lamp's one (read `0x0006`), e.g. after the phone
  travelled or on first setup.
- `0x0005` read gives the lamp's time, "valid" flag and source (RTC / BLE / SNTP / API):
  schedules work only when it is valid.

## 6. Lamp control

`0x0002` Lamp state, write with a mask of fields to apply:

| Action | Bytes |
|---|---|
| Turn on | `01 01 00 00 00 00` |
| Turn off | `01 00 00 00 00 00` |
| Brightness 128 | `04 00 80 00 00 00` |
| Select effect `fire` | `02 00 00 00 00 04 66 69 72 65` |
| Start the demo mode | `10 00 00 00 01 00` |

Layout: mask, on, brightness, fps, demo, str8 effect code (empty when bit 1 is not set).

- The lamp **notifies** the new state on every change: from the app, buttons, schedules, the demo
  mode switching effects, the HTTP API. Use notifications as the source of truth; do not assume a
  write changed what you expect (e.g. selecting an effect turns the demo mode off).
- During a dawn, brightness notifications come up to 4 times per second.
- For a brightness slider, write at most every ~100 ms; the lamp saves settings 3 s after the
  last change, so frequent writes do not wear the flash.
- Next/previous effect respecting the demo mode: Command `0x04` / `0x05`.

## 7. Settings screens

| Screen | Characteristic | Notes |
|---|---|---|
| Schedule | `0x0007` | read, modify, write the whole value; dawn effect by code |
| Demo mode | `0x000D`, `0x000E`, flag in Effect info | order, duration, random addition; include/exclude effects by code; the last included effect cannot be excluded |
| Matrix | `0x0008` | size, LED type, color order, rotation, mirror, current limit, wiring; **reboot to apply** |
| Hardware | `0x0013` | controls (one button / four buttons / touch pad - only those in the "supported" mask), buttons type; **reboot to apply** |
| WiFi | `0x0009`, `0x000A` | mode off / periodic / always on, period, SSID, password (never read back) |
| Name | `0x000B` | up to 20 bytes, **reboot to apply** |
| Device | `0x000C` | reboot (`01`), reset effects (`02`), factory reset (`03`, forgets bonds) |

"Reboot to apply": offer a "Reboot now" button that writes Command `0x01`. After reboot the lamp
is in the pairing window for 2 minutes and the bonded phone reconnects normally.

Matrix orientation for the user: the first LED is at the bottom left, rows go left to right
(serpentine: the next row goes back). For other layouts combine rotation and mirror; showing a
test pattern after reboot helps to find the right combination.

## 8. WiFi (optional)

The lamp is fully usable without WiFi. WiFi gives time sync (SNTP), the internet update and,
in the "always on" mode, the HTTP API (`http://<lamp ip>/api/...`, see [API.md](../API.md)).

- Write `0x0009`: mode, period (hours), SSID, password. To change the mode without retyping the
  password send password length `0xFF` (keep the stored one).
- Watch `0x000A` WiFi status: off / connecting / connected (IP, RSSI) / error (wrong password or
  network not found). In the periodic mode "off" between sessions is normal.

## 9. Firmware update

Two ways; both report progress through `0x0010` OTA control notifications:
state (0 idle, 1 receiving, 2 success - the lamp reboots in 1 s, 3 error, 4 checking,
5 no update), error code, bytes received, total.

**Release files.** Every GitHub release of `golyakoff/FireLamp_ESP32` has `manifest.json`:

```json
{
  "version": "0.1.0",
  "builds": {
    "esp32-default":   { "url": "https://github.com/.../firelamp_v0.1.0_esp32_fw.bin",   "size": 1398064, "sha256": "<64 hex>" },
    "esp32c3-default": { "url": "https://github.com/.../firelamp_v0.1.0_esp32c3_fw.bin", "size": 1538240, "sha256": "<64 hex>" }
  }
}
```

The latest one is at `https://github.com/golyakoff/FireLamp_ESP32/releases/latest/download/manifest.json`.
The build key is `<chip>-<variant>`: chip `esp32` / `esp32c3` from Device info byte 1 (0 / 1),
variant from Device info. **If the key is missing, there is no official firmware for this lamp**
(custom pinout): do not offer an update. Compare `version` with the firmware version from Device
info (`MAJOR.MINOR.PATCH`, numerically).

**A. Upload from the phone over BLE** (no WiFi needed):

1. Download the file from the manifest, check its size and SHA-256.
2. MTU 517, notifications on `0x0010` enabled.
3. Write `0x0010`: `01` + u32 size + the 32 byte SHA-256 (optional, recommended).
4. Wait for status `1` (receiving). The lamp erases the partition first: it takes several seconds.
5. Write the file to `0x0011` in chunks of `MTU - 3` bytes, each **with response**
   (`WRITE_TYPE_DEFAULT`); the response is the flow control. Progress notifications come every 4 KB.
6. Write `0x0010`: `02`. Wait for status `2` (the lamp reboots) or `3` (error code tells why).
7. After the reboot reconnect and check the version in Device info.

`03` aborts. A disconnect also aborts the BLE upload; the old firmware keeps running. (An
internet update started with `0x07` goes on when the phone disconnects.) If the new
firmware does not start, the lamp rolls back to the previous one by itself.

**B. From the internet by the lamp itself** (WiFi configured):

- The lamp checks the manifest on every WiFi session and reports the result in `0x0012` Update
  info: state (not checked / checking / up to date / available / check failed), the latest version,
  the time of the check. Command `0x06` checks now.
- It installs **only** on Command `0x07`. Progress comes in `0x0010` notifications (states `4`
  checking, `1` receiving, `2` success, `3` error with code `8` = download failed, `5` no update).
- Without WiFi configured, `0x06` / `0x07` fail with ATT error `0x13`.

## 10. Differences from Matrix Clock

| | Matrix Clock | FireLamp |
|---|---|---|
| Security | none | bonding + pairing window |
| Time | local time | UTC + POSIX time zone on the lamp |
| Settings | one characteristic per value | grouped binary structures, partial updates by mask |
| Lists | fixed | effects enumerated, identified by text codes |
| OTA start / end / abort | `01`+size / `02` / `03` | same, plus optional SHA-256 |
| OTA status | none (inferred from writes and reconnects) | notifications on `0x0010` with progress and error codes |
| OTA file choice | by flash size (4 / 16 MB) | by chip and build variant via `manifest.json` |
| Internet update | - | by the lamp, on command |
