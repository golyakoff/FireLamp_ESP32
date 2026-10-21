# FireLamp BLE protocol

Version: **1**

The lamp is a BLE GATT server (peripheral) with a single primary service.

## Conventions

- All multi-byte integers are **little-endian**.
- Strings are UTF-8 without a terminating zero. Inside binary structures a string is prefixed
  with its length byte (`str8`); a characteristic holding a single string has no prefix.
- **Effects and their parameters are identified by codes**, never by numbers. A code is up to
  16 characters `a-z`, `0-9`, `_`, e.g. `fire`, `plasma_waves`, `speed`, `palette`. Codes never
  change between firmware versions; effects may be added, removed or reordered, so the app must
  not rely on positions in the effect list. The app may show its own (localized) names by code
  and fall back to the English name sent by the lamp for codes it does not know.
- A write with an invalid length, unknown code or out of range value is rejected with ATT error
  `0x0D Invalid Attribute Value Length` or `0x13 Value Not Allowed`, nothing is changed.

## Advertising and connection

- Device name: user-defined (see [Device name](#0x000b-device-name)), default `FireLamp-XXXX`
  where `XXXX` are the last two bytes of the BLE MAC address in hex.
- The advertising packet carries the 128-bit service UUID, so the app can filter lamps by it.
- One connection at a time. Advertising restarts after disconnect.
- A connection with no requests to the lamp's characteristics for 5 minutes and no enabled
  notifications is dropped by the lamp, so it can advertise again.
- Finding lamps: while a lamp is connected it does not advertise, and Android often keeps the
  link to a just bonded lamp open by itself, with no app using it (until the idle timeout above).
  So the app should list both the lamps found by scanning for the service UUID and the bonded
  lamps from `BluetoothAdapter.getBondedDevices()` (name `FireLamp-*` by default, or any name
  when the service is found after connecting). `connectGatt()` to a bonded lamp works even when
  the system already holds the link: Android shares one link between GATT clients.
- MTU up to 517 is accepted; the app should request it right after connecting
  (the OTA data chunk size depends on it).

## Security

The lamp accepts only **bonded** (paired) phones. Pairing is "Just Works" (no PIN) with LE Secure
Connections and is allowed only in the **pairing window**:

- 2 minutes after power on;
- 2 minutes after the pairing gesture: 5 clicks of the main button (or touch pad); the lamp
  confirms it with a short blink.

Already bonded phones can connect at any time. Up to 4 phones are remembered; bonding a 5th one
forgets the oldest. Factory reset forgets all phones.

How it works for the app:

- All characteristics require an encrypted link. The first access of a not yet bonded phone
  fails with ATT error `0x0F Insufficient Encryption`, and Android starts pairing by itself
  (or call `BluetoothDevice.createBond()` explicitly), showing its system dialog.
- If the pairing window is closed, the pairing completes on the phone's side, but the lamp drops
  the new keys and disconnects right away. The app should then ask the user to make 5 clicks on
  the lamp and connect again (Android may need the stale bond removed: `removeBond()` via
  reflection, or "Forget" in the Bluetooth settings).
- A phone that does not complete pairing within 30 s after connecting is disconnected.

## UUIDs

Service and characteristic UUIDs share one base, `xxxx` is the 16-bit number from the table:

```
F1A7xxxx-B390-49C3-8770-A1DFC323D1AF
```

Service: `F1A70000-B390-49C3-8770-A1DFC323D1AF`

| #      | Characteristic                                   | Props   |
|--------|--------------------------------------------------|---------|
| 0x0001 | [Device info](#0x0001-device-info)               | R       |
| 0x0002 | [Lamp state](#0x0002-lamp-state)                 | R, W, N |
| 0x0003 | [Effect info](#0x0003-effect-info)               | R, W    |
| 0x0004 | [Effect parameter](#0x0004-effect-parameter)     | R, W, N |
| 0x0005 | [Time](#0x0005-time)                             | R, W    |
| 0x0006 | [Time zone](#0x0006-time-zone)                   | R, W    |
| 0x0007 | [Schedule](#0x0007-schedule)                     | R, W    |
| 0x0008 | [LED settings](#0x0008-led-settings)             | R, W    |
| 0x0009 | [WiFi settings](#0x0009-wifi-settings)           | R, W    |
| 0x000A | [WiFi status](#0x000a-wifi-status)               | R, N    |
| 0x000B | [Device name](#0x000b-device-name)               | R, W    |
| 0x000C | [Command](#0x000c-command)                       | W       |
| 0x000D | [Demo settings](#0x000d-demo-settings)           | R, W    |
| 0x000E | [Demo effects](#0x000e-demo-effects)             | W       |
| 0x0010 | [OTA control](#0x0010-ota-control)               | W, N    |
| 0x0011 | [OTA data](#0x0011-ota-data)                     | W       |
| 0x0012 | [Update info](#0x0012-update-info)               | R, N    |
| 0x0013 | [Hardware settings](#0x0013-hardware-settings)   | R, W    |
| 0x0014 | [WiFi networks](#0x0014-wifi-networks) (optional, firmware ≥ 0.1.5) | R, W, N |

Optional characteristics may be missing on older firmware: the app checks for them after
service discovery and hides the feature when one is missing.

R - read, W - write with response, N - notify (the app enables it via the CCCD descriptor).

---

## 0x0001 Device info

Read only, constant for the connection.

| Offset | Type   | Field |
|--------|--------|-------|
| 0      | u8     | Protocol version, `1`. The app must refuse to work with a version it does not know |
| 1      | u8     | Chip: `0` - ESP32, `1` - ESP32-C3 |
| 2      | u8     | Flash size, MB |
| 3      | u16    | Features bitmask, see below |
| 5      | u8     | Matrix width |
| 6      | u8     | Matrix height |
| 7      | u8     | Number of effects |
| 8      | u8     | Controls in use: `0` - one button, `1` - four buttons, `2` - touch pad (see [Hardware settings](#0x0013-hardware-settings)) |
| 9      | str8   | Firmware version, e.g. `0.5.0` |
| ...    | str8   | Build date and time, e.g. `Oct 21 2026 12:00:00` |
| ...    | str8   | Build variant, `default` for official releases (hardware configuration, see README) |

Features bitmask:

| Bit | Meaning |
|-----|---------|
| 0   | DS3231 RTC found on the I2C bus at boot |
| 1   | AT24Cxx EEPROM found on the I2C bus at boot, the settings are stored in it (otherwise in the internal flash) |
| 2   | WiFi supported |
| 3   | Internet firmware update supported |

The app picks the OTA firmware file by chip (offset 1) and build variant: official releases
contain the `default` variant only; do not offer them to a lamp with another variant.

---

## 0x0002 Lamp state

**Read / notify**:

| Offset | Type | Field |
|--------|------|-------|
| 0      | u8   | On: `0` - off, `1` - on |
| 1      | u8   | Brightness `1..255` |
| 2      | u8   | FPS `1..120` |
| 3      | u8   | Demo mode: `0` - off, `1` - on |
| 4      | str8 | Current effect code |

Notified on every change made by anyone: app, buttons, schedule, demo mode, HTTP API. During
automatic brightness changes (dawn) notifications are limited to 4 per second.

**Write**: the first byte tells which of the following fields to apply:

| Offset | Type | Field |
|--------|------|-------|
| 0      | u8   | Mask: bit 0 - on, bit 1 - effect, bit 2 - brightness, bit 3 - FPS, bit 4 - demo mode |
| 1      | u8   | On |
| 2      | u8   | Brightness |
| 3      | u8   | FPS |
| 4      | u8   | Demo mode |
| 5      | str8 | Effect code (may be empty when bit 1 is not set) |

Fields not selected by the mask are ignored. Behaviour:

- Selecting an effect (bit 1) turns the demo mode off, unless bit 4 turns it on in the same write
  (then the demo starts from the selected effect).
- Any write stops a running dawn (manual control has priority).

---

## 0x0003 Effect info

Describes one effect with its parameters. The full list is read effect by effect.

**Write** - select the effect to describe:

| Variant | Bytes | Meaning |
|---------|-------|---------|
| by position | `0x00`, u8 position | Position in the lamp's effect list `0 .. count-1`; only a cursor to enumerate the list |
| by code     | `0x01`, str8 code   | Effect with this code |

**Read**: description of the selected effect (the first one after connecting):

| Type | Field |
|------|-------|
| u8   | Position in the list |
| u8   | Number of effects in the list |
| str8 | Effect code |
| str8 | Effect name (English) |
| u8   | Flags: bit 0 - included in the demo mode |
| u8   | Number of parameters `N` |
| N × parameter | |

Parameter:

| Type | Field |
|------|-------|
| str8 | Parameter code, unique within the effect, e.g. `speed` |
| str8 | Parameter name (English) |
| u8   | Type: `0` - range, `1` - boolean (min 0, max 1) |
| u8   | Min |
| u8   | Max |
| u8   | Default |
| u8   | Current value |

Reading the whole list: write `0x00, 0`, read; take the count from the answer; then for
positions `1 .. count-1`: write `0x00, i`, read. The descriptions (except current values and the
demo flag) do not change while the firmware version is the same, so the app may cache them.

---

## 0x0004 Effect parameter

**Read** - current values of all parameters of the current effect:

| Type | Field |
|------|-------|
| str8 | Effect code |
| u8   | Number of parameters `N` |
| N × (str8 parameter code, u8 value) | |

**Write** - set one parameter of any effect (saved):

| Type | Field |
|------|-------|
| str8 | Effect code |
| str8 | Parameter code |
| u8   | Value, within the parameter's min..max |

**Notify**: the same as write, when a parameter value is changed by anyone (also after the app's
own write). Writing the current value again gives no notification.

---

## 0x0005 Time

**Write**: 4 bytes - u32, current time in seconds since 1970-01-01 **UTC**. The app should write
it on every connection. Also sets the RTC if present.

**Read**: 6 bytes:

| Offset | Type | Field |
|--------|------|-------|
| 0      | u32  | Current time, seconds since 1970-01-01 UTC |
| 4      | u8   | Valid: `1` if the time has been set since boot (schedules work only then) |
| 5      | u8   | Source: `0` - none, `1` - RTC, `2` - BLE, `3` - SNTP, `4` - HTTP API |

---

## 0x0006 Time zone

POSIX TZ string, up to 47 bytes, e.g. `MSK-3`, `CET-1CEST,M3.5.0,M10.5.0/3`.
The lamp keeps UTC and converts it to local time with this string, so daylight saving time is
handled by the lamp. The app should map the phone's time zone (IANA name, e.g. `Europe/Berlin`)
to the POSIX string; a table of common zones is enough.

---

## 0x0007 Schedule

Read and write as a whole (the app reads, modifies, writes back). Local time.

Schedule time (4 bytes):

| Offset | Type | Field |
|--------|------|-------|
| 0      | u8   | Enabled: `0` / `1` |
| 1      | u8   | Days of week: bit 0 - Monday ... bit 6 - Sunday (`0x7F` - every day, `0x1F` - Mon..Fri) |
| 2      | u8   | Hour `0..23` |
| 3      | u8   | Minute `0..59` |

Layout:

| Offset | Size | Field |
|--------|------|-------|
| 0      | 4    | Power on: schedule time |
| 4      | 4    | Power off: schedule time |
| 8      | 4    | Dawn: schedule time - the moment when the final brightness is reached |
| 12     | 1    | Dawn: duration of the brightness ramp, minutes `1..120` |
| 13     | 1    | Dawn: final brightness `1..255` |
| 14     | 1    | Hourly brightness: enabled `0` / `1` |
| 15     | 24   | Hourly brightness: value `1..255` for hours 0..23 |
| 39     | str8 | Dawn: effect code |

Behaviour:

- Power on/off fire once at the given minute; the lamp can be controlled manually in between.
- Dawn turns the lamp on with the given effect (and turns the demo mode off) and raises
  brightness from the minimum to the final value during `duration` minutes. Any manual control
  stops the ramp until the next dawn.
- Hourly brightness is applied when the lamp turns on and at the start of every hour; a manual
  brightness change keeps effect until the next hour.

---

## 0x0008 LED settings

9 bytes. Applied after reboot (send [Command](#0x000c-command) `0x01`).

| Offset | Type | Field |
|--------|------|-------|
| 0      | u8   | Matrix width `8..128` |
| 1      | u8   | Matrix height `8..128`, width × height ≤ 1024 |
| 2      | u8   | LED type: `0` - WS2812, `1` - SK6812, `2` - WS2811 |
| 3      | u8   | Color order: `0` - GRB, `1` - RGB |
| 4      | u8   | Rotation: `0` - none, `1` - 90°, `2` - 180°, `3` - 270° |
| 5      | u8   | Mirror horizontally: `0` / `1` |
| 6      | u16  | Current limit for all LEDs, mA |
| 8      | u8   | Wiring: `0` - serpentine (rows go back and forth), `1` - progressive (every row starts at the same side) |

The first LED is at the bottom left corner, the strip goes along the rows. Other layouts are
described with rotation and mirror: e.g. a matrix wired by columns is rotated by 90°, a matrix
starting at the bottom right corner is mirrored.

---

## 0x0009 WiFi settings

WiFi is optional and off by default; the lamp connects only as a client (station) to a home
network. Modes:

- off;
- periodic: 5 s after boot or a settings change, and then every `period` hours, the lamp connects,
  syncs time by SNTP (stage 7: checks for a firmware update) and disconnects; a failed attempt
  is retried in 10 minutes;
- always on: stays connected, reconnects automatically, keeps time synced, the HTTP API is
  available.

Settings are applied right away. During a BLE firmware upload WiFi is paused.
A static IP address can be configured via the HTTP API only (DHCP by default).

**Write**:

| Offset | Type | Field |
|--------|------|-------|
| 0      | u8   | Mode: `0` - off, `1` - periodic (connect every N hours for time sync and update check, then disconnect), `2` - always on (HTTP API available) |
| 1      | u8   | Period for mode 1, hours `1..24` |
| 2      | str8 | SSID, up to 32 bytes |
| ...    | str8 | Password, up to 64 bytes. Length `0xFF` - keep the stored password |

**Read**: the same, but the password is never returned: its length byte is `0` (no password
stored) or `0xFF` (a password is stored), followed by no bytes.

## 0x000A WiFi status

**Read / notify**:

| Offset | Type  | Field |
|--------|-------|-------|
| 0      | u8    | State: `0` - off (also between sessions of the periodic mode), `1` - connecting, `2` - connected, `3` - error (wrong password, network not found; kept until the next attempt) |
| 1      | u8[4] | IPv4 address, zeros if not connected |
| 5      | i8    | RSSI, dBm, 0 if not connected |

---

## 0x000B Device name

Up to 20 bytes UTF-8, used in advertising. Applied after reboot.
Writing an empty value restores the default `FireLamp-XXXX`.

---

## 0x000C Command

Write; the first byte is the command:

| Code | Command |
|------|---------|
| 0x01 | Reboot |
| 0x02 | Reset parameters of all effects to defaults |
| 0x03 | Factory reset: all settings to defaults, forget bonded phones, reboot |
| 0x04 | Next effect (in the demo mode: switch now, the demo continues) |
| 0x05 | Previous effect (same) |
| 0x06 | Check for a firmware update on the internet now; the result comes in [Update info](#0x0012-update-info). Needs WiFi configured (otherwise `0x13`); in the periodic WiFi mode the lamp connects right away |
| 0x07 | Install the firmware update from the internet, if a newer version is released; progress comes in [OTA control](#0x0010-ota-control) notifications. Needs WiFi configured (otherwise `0x13`) |
| 0x08 | Scan for WiFi networks, see [WiFi networks](#0x0014-wifi-networks) (firmware ≥ 0.1.5). Works in any WiFi mode. `0x13` during a firmware upload over BLE; a repeated command while scanning is accepted and does nothing |

---

## 0x000D Demo settings

The demo mode is not an effect but a mode of the lamp: it plays the included effects one after
another. It is turned on and off via [Lamp state](#0x0002-lamp-state) (bit 4), the current effect
is reported there as usual. The demo mode survives reboot.

**Read / write**: 5 bytes

| Offset | Type | Field |
|--------|------|-------|
| 0      | u8   | Order: `0` - sequential (in the lamp's list order), `1` - random (never the same effect twice in a row) |
| 1      | u16  | Duration of every effect, seconds `10..3600` |
| 3      | u16  | Random addition, seconds `0..3600`: every effect plays `duration + random(0..addition)` |

Buttons and [Command](#0x000c-command) `0x04`/`0x05` switch the effect right away and keep the
demo running; selecting a particular effect from the app turns the demo off.

## 0x000E Demo effects

Which effects take part in the demo mode. By default all effects are included. The current state
is the flag in [Effect info](#0x0003-effect-info).

**Write**:

| Type | Field |
|------|-------|
| str8 | Effect code |
| u8   | `1` - include, `0` - exclude |

Excluding the last included effect is rejected (`0x13`). Effects added by a firmware update are
included.

---

## 0x0010 OTA control

Firmware update. There are two ways, both write the new firmware to the inactive OTA partition;
a failed or aborted update leaves the current firmware untouched, and if the new firmware does
not start properly the bootloader rolls back to the previous one:

1. **From the phone over BLE** (works without WiFi): the app downloads the release `.bin` for the
   lamp's chip and flash size (see [Device info](#0x0001-device-info)) and uploads it as
   described below.
2. **From the internet over WiFi**: the lamp checks the latest GitHub release on every WiFi
   session (see [Update info](#0x0012-update-info)) but installs it **only** on
   [Command](#0x000c-command) `0x07`. The status notifications below are sent in this case too
   (states `4` checking, `5` no update, error `8` download failed), so the app shows the progress
   the same way.

**Write** (BLE upload only):

| Command | Arguments | Meaning |
|---------|-----------|---------|
| `0x01` start | u32 firmware size, optionally u8[32] SHA-256 of the file | Prepare the inactive OTA partition |
| `0x02` end   | - | All data sent: verify, switch the boot partition and reboot |
| `0x03` abort | - | Cancel, discard the received data |

**Notify** (status):

| Offset | Type | Field |
|--------|------|-------|
| 0      | u8   | State: `0` - idle, `1` - receiving, `2` - success (the lamp reboots in 1 s), `3` - error, `4` - checking for update (internet), `5` - no update available (internet) |
| 1      | u8   | Error code (state 3): `1` - busy, `2` - bad size, `3` - flash write error, `4` - image invalid, `5` - SHA-256 mismatch, `6` - timeout (no data for 10 s), `7` - received more data than announced, `8` - download failed (internet) |
| 2      | u32  | Bytes received |
| 6      | u32  | Total size |

Progress notifications are sent every 4 KB.

BLE upload:

1. Request MTU 517, then write `0x01` + size (+ SHA-256).
2. Wait for status `1` (receiving).
3. Write the file in chunks of up to `MTU - 3` bytes to [OTA data](#0x0011-ota-data), each
   **with response** (the response is the flow control: the lamp answers after the chunk is
   queued for writing to flash).
4. Write `0x02`, wait for status `2` or `3`.

During the transfer the lamp blanks the LED matrix, pauses WiFi and asks the phone for a longer
supervision timeout (lessons from Matrix Clock OTA).

## 0x0011 OTA data

Write: next chunk of the firmware file, up to `MTU - 3` bytes. Only accepted between OTA start
and end. A disconnect aborts a BLE upload (not an internet update).

---

## 0x0012 Update info

Result of the last internet update check. The lamp checks on every WiFi session (periodic mode:
every `period` hours; always on: after connecting and then every `period` hours) and on
[Command](#0x000c-command) `0x06`.

**Read / notify**:

| Type | Field |
|------|-------|
| u8   | State: `0` - not checked since boot, `1` - checking, `2` - up to date, `3` - update available, `4` - check failed |
| str8 | Latest released version (empty if never checked) |
| u32  | Time of the last successful check, UTC seconds, `0` if unknown |

The current version is in [Device info](#0x0001-device-info). The app can offer the update when
the state is `3` and install it with command `0x07`.

---

## 0x0013 Hardware settings

The pins are fixed in the firmware (see the README for the reference pinout of each chip);
the control elements connected to them are a setting. Applied after reboot (send
[Command](#0x000c-command) `0x01`).

**Read**: 3 bytes

| Offset | Type | Field |
|--------|------|-------|
| 0      | u8   | Controls: `0` - one button (or TTP223 module), `1` - four buttons (Main, Up, Down, Reset), `2` - ESP32 touch pad |
| 1      | u8   | Supported controls bitmask: bit N - controls `N` are supported by this chip (no touch pad on ESP32-C3) |
| 2      | u8   | Buttons type (Main, Up, Down): `1` - active high: TTP223 touch module or a button to +3.3 V, `0` - active low: a button to GND. The internal pull resistor is enabled towards the opposite level |

**Write**: 2 bytes - controls and buttons type (offsets 0 and 2 of the read value). An
unsupported value is rejected (`0x13`). The Reset button (four buttons mode) is always active
low: it is the BOOT button in the reference pinout.

The DS3231 RTC and the AT24Cxx EEPROM need no settings: they are detected on the I2C bus at
boot, see [Device info](#0x0001-device-info) features.

---

## 0x0014 WiFi networks

Optional, firmware ≥ 0.1.5. The networks the lamp hears, for choosing the SSID in the WiFi
settings: the lamp sees 2.4 GHz networks only, so 5 GHz networks never get in the list.

The scan is started by [Command](#0x000c-command) `0x08`. It works in any WiFi mode: when WiFi is
off or between the sessions of the periodic mode the radio is turned on for the scan only and off
again; an always-on connection is kept (while the lamp is connecting the scan fails with state
`3`, try again); during a periodic session the scan starts right after it. A scan takes a few
seconds, `3` is reported if there is no result in 10 s. The result is kept in RAM until the next
scan.

The list has unique SSIDs (the strongest access point of each), no hidden networks, strongest
first, up to 64 networks. It is read page by page.

**Read**: one page, starting at the selected network (the first one after connecting and after
every scan):

| Offset | Type | Field |
|--------|------|-------|
| 0      | u8   | State: `0` - not scanned since boot, `1` - scanning, `2` - done, `3` - error |
| 1      | u8   | Total number of networks `T` (0 unless the state is `2`) |
| 2      | u8   | Index of the first network on this page |
| 3      | u8   | Number of networks on this page `N` (as many as fit in 512 bytes) |
| 4      | N × network | |

Network:

| Type | Field |
|------|-------|
| str8 | SSID |
| i8   | RSSI, dBm |
| u8   | Security: `0` - open (also OWE), `1` - WEP, `2` - WPA/WPA2/WPA3 personal (password), `3` - enterprise (the lamp cannot connect) |
| u8   | Channel |

**Write**: u8 - index of the first network of the next page to read, `0 .. T`. Rejected with
`0x13` unless the state is `2`.

**Notify**: the header only (4 bytes, `N` = 0) when the scan starts (state `1`) and finishes
(`2` or `3`).

To get the list: subscribe, write command `0x08`, wait for the notification with state `2`, read;
while `index + N < T`, write `index + N` and read again.
