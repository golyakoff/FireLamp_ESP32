"""Time, schedule, LED/hardware/WiFi settings, device name: round trips and validation.
Only the same values are written back, nothing needs a reboot."""

import time

import pytest

import firelamp as fl


def test_time(lamp):
    now = int(time.time())
    lamp.write(fl.TIME, fl.u32(now))
    r = fl.Reader(lamp.read(fl.TIME))
    t, valid, source = r.u32(), r.u8(), r.u8()
    r.end()
    assert abs(t - now) <= 2
    assert valid == 1
    assert source == 2, "BLE"
    assert lamp.write_error(fl.TIME, b"\x00\x00\x00") == fl.ATT_INVALID_LENGTH


@pytest.fixture
def time_zone(lamp):
    orig = lamp.read(fl.TIME_ZONE)
    yield orig
    lamp.write(fl.TIME_ZONE, orig)


def test_time_zone(lamp, time_zone):
    assert time_zone, "a time zone is always set"
    tz = b"CET-1CEST,M3.5.0,M10.5.0/3"
    lamp.write(fl.TIME_ZONE, tz)
    assert lamp.read(fl.TIME_ZONE) == tz
    assert lamp.write_error(fl.TIME_ZONE, b"") == fl.ATT_INVALID_LENGTH
    assert lamp.write_error(fl.TIME_ZONE, b"X" * 48) == fl.ATT_INVALID_LENGTH
    assert lamp.write_error(fl.TIME_ZONE, b"MSK\x01-3") == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.read(fl.TIME_ZONE) == tz


def test_schedule(lamp, effects):
    orig = lamp.read(fl.SCHEDULE)
    r = fl.Reader(orig)
    times = [r.bytes(4) for _ in range(3)]
    duration, brightness, hourly = r.u8(), r.u8(), r.u8()
    values = r.bytes(24)
    effect = r.str8()
    r.end()
    for t in times:
        assert t[0] in (0, 1) and t[1] <= 0x7F and t[2] <= 23 and t[3] <= 59
    assert 1 <= duration <= 120 and brightness >= 1 and hourly in (0, 1)
    assert all(v >= 1 for v in values)
    assert effect in {e.code for e in effects}

    lamp.write(fl.SCHEDULE, orig)
    assert lamp.read(fl.SCHEDULE) == orig

    bad_hour = bytearray(orig)
    bad_hour[2] = 24
    bad_effect = orig[:39] + fl.str8("no_such_effect")
    bad_duration = bytearray(orig)
    bad_duration[12] = 0
    for bad in (bytes(bad_hour), bad_effect, bytes(bad_duration)):
        assert lamp.write_error(fl.SCHEDULE, bad) == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.write_error(fl.SCHEDULE, orig[:-1]) == fl.ATT_INVALID_LENGTH
    assert lamp.read(fl.SCHEDULE) == orig


def test_led_settings(lamp):
    orig = lamp.read(fl.LED_SETTINGS)
    assert len(orig) == 9
    width, height, typ, order, rotation, mirror, limit, wiring = \
        orig[0], orig[1], orig[2], orig[3], orig[4], orig[5], int.from_bytes(orig[6:8], "little"), orig[8]
    assert typ <= 2 and order <= 1 and rotation <= 3 and mirror <= 1 and wiring <= 1
    assert limit > 0

    lamp.write(fl.LED_SETTINGS, orig)

    def changed(offset, value):
        b = bytearray(orig)
        b[offset] = value
        return bytes(b)

    for bad in (changed(0, 7), changed(1, 129), changed(2, 3), changed(3, 2), changed(4, 4),
                changed(5, 2), changed(8, 2), bytes([128, 128]) + orig[2:]):
        assert lamp.write_error(fl.LED_SETTINGS, bad) == fl.ATT_VALUE_NOT_ALLOWED, bad.hex()
    assert lamp.write_error(fl.LED_SETTINGS, orig[:8]) == fl.ATT_INVALID_LENGTH
    assert lamp.read(fl.LED_SETTINGS) == orig


def test_hardware_settings(lamp, info):
    orig = lamp.read(fl.HARDWARE)
    mode, supported, level, gpio, count = orig[:5]
    gpios = list(orig[5:])
    assert len(gpios) == count and gpios == sorted(gpios) and gpio in gpios
    assert supported & (1 << mode)
    assert level in (0, 1)
    if info.chip == 1:
        assert not supported & 0b100, "no touch pad on ESP32-C3"
    for taken in (5, 0, 34) if info.chip == 0 else (5, 9, 18):
        assert taken not in gpios, f"GPIO {taken} must not be offered for the Main button"

    # the 2 bytes form of older apps keeps the pin
    lamp.write(fl.HARDWARE, bytes([mode, level]))
    assert lamp.read(fl.HARDWARE) == orig
    other = next((g for g in gpios if g != gpio), gpio)
    lamp.write(fl.HARDWARE, bytes([mode, level, other]))
    assert lamp.read(fl.HARDWARE)[3] == other
    lamp.write(fl.HARDWARE, bytes([mode, level, gpio]))

    unsupported = [m for m in range(3) if not supported & (1 << m)]
    bad_pins = [g for g in range(40) if g not in gpios]
    for bad in [bytes([m, level]) for m in unsupported] + [bytes([3, level]), bytes([mode, 2])] \
            + [bytes([mode, level, g]) for g in bad_pins]:
        assert lamp.write_error(fl.HARDWARE, bad) == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.write_error(fl.HARDWARE, bytes([mode])) == fl.ATT_INVALID_LENGTH
    assert lamp.write_error(fl.HARDWARE, bytes([mode, level, gpio, 0])) == fl.ATT_INVALID_LENGTH
    assert lamp.read(fl.HARDWARE) == orig


def test_wifi_settings(lamp):
    orig = lamp.read(fl.WIFI_SETTINGS)
    r = fl.Reader(orig)
    mode, period, ssid, pw = r.u8(), r.u8(), r.str8(), r.u8()
    r.end()
    assert mode <= 2 and 1 <= period <= 24
    assert pw in (0, 0xFF), "the password is never returned"

    ssid8 = fl.str8(ssid)
    assert lamp.write_error(fl.WIFI_SETTINGS, bytes([1, 1]) + fl.str8("") + b"\xff") == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.write_error(fl.WIFI_SETTINGS, bytes([mode, 0]) + ssid8 + b"\xff") == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.write_error(fl.WIFI_SETTINGS, bytes([mode, 25]) + ssid8 + b"\xff") == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.write_error(fl.WIFI_SETTINGS, bytes([3, period]) + ssid8 + b"\xff") == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.write_error(fl.WIFI_SETTINGS, bytes([mode, period]) + fl.str8("x" * 33) + b"\xff") \
        == fl.ATT_INVALID_LENGTH
    assert lamp.read(fl.WIFI_SETTINGS) == orig


def test_wifi_commands_without_wifi(lamp):
    r = fl.Reader(lamp.read(fl.WIFI_SETTINGS))
    mode, _, ssid = r.u8(), r.u8(), r.str8()
    if mode != 0 and ssid:
        pytest.skip("WiFi is configured")
    assert lamp.write_error(fl.COMMAND, bytes([fl.CMD_CHECK_UPDATE])) == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.write_error(fl.COMMAND, bytes([fl.CMD_INSTALL_UPDATE])) == fl.ATT_VALUE_NOT_ALLOWED


def test_device_name(lamp):
    # not written back: that would store the default name as a custom one
    orig = lamp.read(fl.DEVICE_NAME)
    assert 0 < len(orig) <= 20
    assert lamp.write_error(fl.DEVICE_NAME, b"x" * 21) == fl.ATT_INVALID_LENGTH
    assert lamp.write_error(fl.DEVICE_NAME, b"bad\x01name") == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.read(fl.DEVICE_NAME) == orig


def test_power_status(lamp):
    if not lamp.has(fl.POWER):
        pytest.skip("firmware < 0.9.0")
    p = fl.parse_power(lamp.read(fl.POWER))
    assert 30 <= p.percent <= 100
    assert p.brownout in (0, 1)
    assert p.calib_state in (0, 2, 3), "no calibration runs during the tests"
    assert p.calib_steps == 10


def test_power_rejects(lamp):
    if not lamp.has(fl.POWER):
        pytest.skip("firmware < 0.9.0")
    p = fl.parse_power(lamp.read(fl.POWER))
    assert lamp.write_error(fl.POWER, b"\x01\x1d") == fl.ATT_VALUE_NOT_ALLOWED  # below 30 %
    assert lamp.write_error(fl.POWER, b"\x01\x65") == fl.ATT_VALUE_NOT_ALLOWED  # above 100 %
    assert lamp.write_error(fl.POWER, b"\x01") == fl.ATT_INVALID_LENGTH
    assert lamp.write_error(fl.POWER, b"\x03") == fl.ATT_VALUE_NOT_ALLOWED  # no calibration runs
    assert lamp.write_error(fl.POWER, b"\x09") == fl.ATT_VALUE_NOT_ALLOWED
    # the same value again is accepted
    lamp.write(fl.POWER, bytes([1, p.percent]))
    assert fl.parse_power(lamp.read(fl.POWER)).percent == p.percent
