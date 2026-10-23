# API

FireLamp provides a simple API based on HTTP GET and POST requests containing JSON data.
It is available when WiFi is in the "always on" mode (see [WiFi settings](#set-wifi-settings));
the main control channel is BLE, see [docs/BLE_PROTOCOL.md](docs/BLE_PROTOCOL.md).

---

## Contents

- [Get firmware information](#get-firmware-information)
- [Get current WiFi settings](#get-current-wifi-settings)
- [Set WiFi settings](#set-wifi-settings)
- [Get current LED settings](#get-current-led-settings)
- [Set LED settings](#set-led-settings)
- [Reset system settings](#reset-system-settings)
- [Reboot device](#reboot-device)
- [Get current lamp state](#get-current-lamp-state)
- [Set lamp state](#set-lamp-state)
- [Get full list of effects and its parameters](#get-full-list-of-effects-and-its-parameters)
- [Reset parameters of all effects](#reset-parameters-of-all-effects)
- [Get current effect parameters](#get-current-effect-parameters)
- [Set effect parameters](#set-effect-parameters)
- [Get time](#get-time)
- [Set time and time zone](#set-time-and-time-zone)
- [Get schedule](#get-schedule)
- [Set schedule](#set-schedule)
- [Get demo mode settings](#get-demo-mode-settings)
- [Set demo mode settings](#set-demo-mode-settings)
- [Firmware update](#firmware-update)
- [Hardware settings](#hardware-settings)

---

## Get firmware information

### Request

**GET** `/api/info`

### Response

```jsonc
{
  "app_name": <String, Application name, usually "FireLamp">,
  "app_version": <String, Application version>,
  "build_date": <String, Application build date>,
  "idf_ver": <String, Version of used ESP-IDF>
}
```

**Example**

```json
{
  "app_name": "FireLamp",
  "app_version": "0.6.3",
  "build_date": "May  2 2022",
  "idf_ver": "v5.0-dev-1829-ge29e96101d-dirty"
}
```

---

## Get current WiFi settings

The lamp connects to a home network as a client only. WiFi is optional (the lamp is controlled
over BLE); this API is available in the "always on" mode.

### Request

**GET** `/api/settings/wifi`

### Response

```jsonc
{
  "mode": <Integer, 0 - off, 1 - periodic (connect every `period` hours for time sync, then disconnect), 2 - always on>,
  "period": <Integer, hours, 1..24, for the periodic mode>,
  "ssid": <String, network name>,
  "password_set": <Boolean, a password is stored; the password itself is never returned>,
  "dhcp": <Boolean, get IP address from DHCP>,
  "ip": <String, static IP address, used when DHCP is off>,
  "netmask": <String>,
  "gateway": <String>,
  "dns": <String>,
  "status": {
    "state": <Integer, 0 - off, 1 - connecting, 2 - connected, 3 - error (wrong password, network not found)>,
    "ip": <String, current IP address, "0.0.0.0" if not connected>,
    "rssi": <Integer, signal level, dBm>
  }
}
```

---

## Set WiFi settings

All fields are optional, missing ones keep their values. Nothing is changed if anything is
invalid. The settings are applied right away, so the connection the request came through may be
dropped after the response.

### Request

**POST** `/api/settings/wifi`

```jsonc
{
  "mode": <Integer>,
  "period": <Integer>,
  "ssid": <String, up to 32 bytes, required unless mode is 0>,
  "password": <String, up to 64 bytes, empty for an open network>,
  "dhcp": <Boolean>,
  "ip": <String>,
  "netmask": <String>,
  "gateway": <String>,
  "dns": <String>
}
```

**Example**

```json
{ "mode": 2, "ssid": "MyHomeWiFi", "password": "SuperSecret" }
```

### Response

```jsonc
{
  "result": <Integer, Result code. 0 - no errors>,
  "name": <String, Name of the result code, "ESP_OK" for result code 0>,
  "message": <String, Error message if result code != 0>
}
```

---

## Get current LED settings

### Request

**GET** `/api/settings/leds`

### Response

```jsonc
{
  "width": <Integer, matrix width (number of columns), 8..128>,
  "height": <Integer, matrix height (number of rows), 8..128>,
  "type": <Integer, type of LEDs. 0 - WS2812, 1 - SK6812, 2 - WS2811 (also APA106, SM16703)>,
  "color_order": <Integer, order of color components. 0 - GRB, 1 - RGB>,
  "rotation": <Integer, surface rotation. 0 - none, 1 - 90, 2 - 180, 3 - 270 degrees>,
  "mirror": <Boolean, mirror surface horizontally>,
  "current_limit": <Integer, current limit of the whole lamp (power supply) in milliamps. A part is
                   kept for the controller and the unlit LEDs, the brightness of every effect is
                   capped by its brightest frame. The larger this value, the higher the maximum
                   brightness.>,
  "wiring": <Integer, 0 - serpentine (rows go back and forth), 1 - progressive (every row starts at the same side)>
}
```

*Note: width * height should not be more than 1024*

**Example**

Single 16x16 WS2812B matrix:

```json
{
  "width": 16,
  "height": 16,
  "type": 0,
  "color_order": 0,
  "rotation": 0,
  "mirror": false,
  "current_limit": 5000
}
```

---

## Set LED settings

Settings are applied after reboot.

### Request

**POST** `/api/settings/leds`

```jsonc
{
  "width": <Integer, matrix width (number of columns), 8..128>,
  "height": <Integer, matrix height (number of rows), 8..128>,
  "type": <Integer, type of LEDs. 0 - WS2812, 1 - SK6812, 2 - WS2811 (also APA106, SM16703)>,
  "color_order": <Integer, order of color components. 0 - GRB, 1 - RGB>,
  "rotation": <Integer, surface rotation. 0 - none, 1 - 90, 2 - 180, 3 - 270 degrees>,
  "mirror": <Boolean, mirror surface horizontally>,
  "current_limit": <Integer, current limit of the whole lamp (power supply) in milliamps. A part is
                   kept for the controller and the unlit LEDs, the brightness of every effect is
                   capped by its brightest frame. The larger this value, the higher the maximum
                   brightness.>,
  "wiring": <Integer, 0 - serpentine (rows go back and forth), 1 - progressive (every row starts at the same side)>
}
```

`color_order`, `mirror` and `wiring` are optional, current values are kept when they are omitted.

### Response

```jsonc
{
  "result": <Integer, Result code. 0 - no errors>,
  "name": <String, Name of the result code, "ESP_OK" for result code 0>,
  "message": <String, Error message if result code != 0>
}
```

---

## Reset system settings

Resets all system settings including both WiFi and LED settings to default values.
After resetting device must be rebooted.

### Request

**GET** `/api/settings/reset`

### Response

```jsonc
{
  "result": <Integer, Result code. 0 - no errors>,
  "name": <String, Name of the result code, "ESP_OK" for result code 0>,
  "message": <String, Error message if result code != 0>
}
```

---

## Reboot device

### Request

**GET** `/api/reboot`

### Response

No response, device just reboots.

---

## Get current lamp state

### Request

**GET** `/api/lamp/state`

### Response

```jsonc
{
  "on": <Boolean, true - Lamp is on, false - Lamp is off>,
  "effect": <String, current effect code>,
  "brightness": <Integer, LED matrix brightness, 1..255>,
  "fps": <Integer, Frames per seconds, 1..120>,
  "demo": <Boolean, demo mode is on>
}
```

---

## Set lamp state

With this function you can switch LEDs on and off, change current effect, brightness, FPS
and turn the demo mode on and off.

### Request

**POST** `/api/lamp/state`

```jsonc
{
  "on": <Boolean, true - Lamp is on, false - Lamp is off>,
  "effect": <String, effect code>,
  "brightness": <Integer, LED matrix brightness, 1..255>,
  "fps": <Integer, Frames per seconds, 1..120>,
  "demo": <Boolean, demo mode>
}
```

Any item in request can be omitted. Selecting an effect turns the demo mode off unless
`"demo": true` is in the same request. Any request stops a running dawn.

For example, you can switch off the lamp with this POST:
```json
{ "on": false }
```
or change effect to "Fire" and set brightness:
```json
{ "effect": "fire", "brightness": 50 }
```

### Response

```jsonc
{
  "result": <Integer, Result code. 0 - no errors>,
  "name": <String, Name of the result code, "ESP_OK" for result code 0>,
  "message": <String, Error message if result code != 0>
}
```

---

## Get full list of effects and its parameters

### Request

**GET** `/api/effects`

### Response

```jsonc
[
  {
    "code": <String, effect code, e.g. "fire">,
    "name": <String, Effect name>,
    "demo": <Boolean, the effect takes part in the demo mode>,
    "params": [
      {
        "code": <String, parameter code, unique within the effect, e.g. "speed">,
        "name": <String, Parameter name>,
        "min": <Integer, Minimal value>,
        "max": <Integer, Maximal value>,
        "default": <Integer, Default value>,
        "value": <Integer, Current value>,
        "type": <Integer, 0 - range, 1 - boolean>
      },
      ...
    ]
  },
  ...
]
```

Effects and parameters are identified by their codes: up to 16 characters `a-z`, `0-9`, `_`.
Codes never change between firmware versions, while the order of the list may change.

**Example** (shortened)

```json
[{
    "code": "lamp",
    "name": "Color lamp",
    "demo": true,
    "params": [
      { "code": "red", "name": "Red", "min": 0, "max": 255, "default": 255, "value": 255, "type": 0 },
      { "code": "green", "name": "Green", "min": 0, "max": 255, "default": 159, "value": 159, "type": 0 },
      { "code": "blue", "name": "Blue", "min": 0, "max": 255, "default": 70, "value": 70, "type": 0 }
    ]
  },
  {
    "code": "fire",
    "name": "Fire",
    "demo": true,
    "params": [
      { "code": "palette", "name": "Palette", "min": 0, "max": 7, "default": 0, "value": 0, "type": 0 },
      { "code": "scale", "name": "Scale", "min": 10, "max": 100, "default": 60, "value": 60, "type": 0 }
    ]
  }]
```

---

## Reset parameters of all effects

### Request

**GET** `/api/effects/reset`

### Response

```jsonc
{
  "result": <Integer, Result code. 0 - no errors>,
  "name": <String, Name of the result code, "ESP_OK" for result code 0>,
  "message": <String, Error message if result code != 0>
}
```

---

## Get current effect parameters

### Request

**GET** `/api/lamp/effect`

### Response

```jsonc
{
  "effect": <String, current effect code>,
  "params": [ <parameters in the same format as in the effects list> ]
}
```

---

## Set effect parameters

### Request

**POST** `/api/lamp/effect`

```jsonc
{
  "effect": <String, effect code, optional: the current effect if omitted>,
  "params": {
    "<parameter code>": <Integer, value>,
    ...
  }
}
```

Parameters that are not mentioned keep their values. Nothing is changed if any code or value
is invalid.

**Example**

Set palette 1 and scale 60 for the "Fire" effect:

```json
{
  "effect": "fire",
  "params": { "palette": 1, "scale": 60 }
}
```

### Response

```jsonc
{
  "result": <Integer, Result code. 0 - no errors>,
  "name": <String, Name of the result code, "ESP_OK" for result code 0>,
  "message": <String, Error message if result code != 0>
}
```

---

## Get time

### Request

**GET** `/api/time`

### Response

```jsonc
{
  "time": <Integer, current time, seconds since 1970-01-01 UTC>,
  "local": <String, local time "YYYY-MM-DD HH:MM:SS">,
  "tz": <String, POSIX time zone>,
  "valid": <Boolean, time has been set since boot (RTC, phone, internet or API)>,
  "source": <String, where the time came from: "none", "rtc", "ble", "sntp", "api">,
  "rtc": <Boolean, DS3231 RTC is present>
}
```

Schedules work only when `valid` is true.

---

## Set time and time zone

Both fields are optional. The time is also written to the RTC when it is present.

### Request

**POST** `/api/time`

```jsonc
{
  "time": <Integer, seconds since 1970-01-01 UTC>,
  "tz": <String, POSIX time zone, e.g. "MSK-3" or "CET-1CEST,M3.5.0,M10.5.0/3", max 47 chars>
}
```

Example (Linux): `curl -d "{\"time\": $(date +%s)}" http://<lamp>/api/time`

### Response

```jsonc
{
  "result": <Integer, Result code. 0 - no errors>,
  "name": <String, Name of the result code, "ESP_OK" for result code 0>,
  "message": <String, Error message if result code != 0>
}
```

---

## Get schedule

### Request

**GET** `/api/schedule`

### Response

```jsonc
{
  "power_on": <Schedule time, turn the lamp on>,
  "power_off": <Schedule time, turn the lamp off>,
  "dawn": {
    <Schedule time fields: moment when the full brightness is reached>,
    "duration": <Integer, minutes of brightness ramp before that moment, 1..120>,
    "effect": <String, effect code>,
    "brightness": <Integer, final brightness, 1..255>
  },
  "hourly_brightness": {
    "enabled": <Boolean>,
    "values": <Array of 24 integers 1..255, brightness for every hour of the day>
  }
}
```

Schedule time:

```jsonc
{
  "enabled": <Boolean>,
  "days": <Integer, days of week bitmask: bit 0 - Monday ... bit 6 - Sunday, 127 - every day>,
  "hour": <Integer, 0..23, local time>,
  "minute": <Integer, 0..59>
}
```

Notes:

- Power on/off fire once at the given minute; the lamp can be controlled manually in between.
- Dawn turns the lamp on with the given effect and raises brightness from minimum to `brightness`
  over `duration` minutes. Any manual control (buttons, app, API) stops the brightness ramp
  until the next dawn.
- Hourly brightness is applied when the lamp is turned on and at the start of every hour.
  Manual brightness change keeps effect until the next hour.

---

## Set schedule

All sections and fields are optional, missing ones keep their current values.

### Request

**POST** `/api/schedule`

Same format as the response of [Get schedule](#get-schedule).

**Example**

Dawn on working days at 07:00 for 30 minutes:

```json
{
  "dawn": { "enabled": true, "days": 31, "hour": 7, "minute": 0, "duration": 30 }
}
```

### Response

```jsonc
{
  "result": <Integer, Result code. 0 - no errors>,
  "name": <String, Name of the result code, "ESP_OK" for result code 0>,
  "message": <String, Error message if result code != 0>
}
```

---

## Get demo mode settings

The demo mode plays the included effects one after another. It is turned on and off via
[Set lamp state](#set-lamp-state) (`"demo": true/false`).

### Request

**GET** `/api/demo`

### Response

```jsonc
{
  "active": <Boolean, demo mode is on>,
  "order": <Integer, 0 - sequential, 1 - random (never the same effect twice in a row)>,
  "duration": <Integer, seconds every effect plays, 10..3600>,
  "random": <Integer, seconds, 0..3600: every effect plays duration + random(0..random)>,
  "excluded": [ <String, codes of effects that do not take part in the demo> ]
}
```

---

## Set demo mode settings

All fields are optional.

### Request

**POST** `/api/demo`

```jsonc
{
  "order": <Integer>,
  "duration": <Integer>,
  "random": <Integer>,
  "include": {
    "<effect code>": <Boolean, true - include, false - exclude>,
    ...
  }
}
```

Excluding the last included effect fails. Effects added by firmware updates are included.

**Example**

Every effect for 2 minutes plus up to 1 minute, without GIF images:

```json
{ "duration": 120, "random": 60, "include": { "gif": false } }
```

### Response

```jsonc
{
  "result": <Integer, Result code. 0 - no errors>,
  "name": <String, Name of the result code, "ESP_OK" for result code 0>,
  "message": <String, Error message if result code != 0>
}
```

---

## Firmware update

The lamp checks the latest GitHub release on every WiFi session; it installs an update only on
request.

### Request

**GET** `/api/update`

### Response

```jsonc
{
  "current": <String, current firmware version>,
  "state": <String, "unknown" | "checking" | "up_to_date" | "available" | "check_failed">,
  "available": <String, latest released version, empty if never checked>,
  "checked_at": <Integer, time of the last successful check, UTC seconds>
}
```

### Request

**POST** `/api/update`

```jsonc
{ "action": <String, "check" - check now, "install" - install the update if a newer version is released> }
```

The lamp reboots after a successful installation.

### Response

```jsonc
{
  "result": <Integer, Result code. 0 - no errors>,
  "name": <String, Name of the result code, "ESP_OK" for result code 0>,
  "message": <String, Error message if result code != 0>
}
```

---

## Hardware settings

The control elements connected to the fixed pins and the pin of the Main button. The DS3231 RTC
and the AT24Cxx EEPROM are detected automatically.

### Request

**GET** `/api/settings/hardware`

### Response

```jsonc
{
  "input_mode": <Integer, 0 - one button, 1 - four buttons, 2 - touch pad (ESP32 only)>,
  "input_mode_active": <Integer, controls in use now (input_mode is applied after reboot)>,
  "input_modes": [ <Integer, supported values> ],
  "button_level": <Integer, 1 - TTP223 or a button to +3.3V (active high), 0 - a button to GND (active low)>,
  "main_gpio": <Integer, GPIO of the Main button (one button, TTP223, or Main of four buttons)>,
  "main_gpios": [ <Integer, GPIOs the Main button can be connected to, ascending> ]
}
```

### Request

**POST** `/api/settings/hardware`

```jsonc
{ "input_mode": <Integer>, "button_level": <Integer>, "main_gpio": <Integer, one of main_gpios> }
```

All fields are optional.

Applied after reboot.

### Response

```jsonc
{
  "result": <Integer, Result code. 0 - no errors>,
  "name": <String, Name of the result code, "ESP_OK" for result code 0>,
  "message": <String, Error message if result code != 0>
}
```
