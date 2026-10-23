"""Device info and the effects list: read only"""

import re

import firelamp as fl

CODE = re.compile(r"^[a-z0-9_]{1,16}$")


def test_all_characteristics_present(lamp):
    required = {fl.DEVICE_INFO, fl.LAMP_STATE, fl.EFFECT_INFO, fl.EFFECT_PARAM, fl.TIME,
                fl.TIME_ZONE, fl.SCHEDULE, fl.LED_SETTINGS, fl.WIFI_SETTINGS, fl.WIFI_STATUS,
                fl.DEVICE_NAME, fl.COMMAND, fl.DEMO_SETTINGS, fl.DEMO_EFFECTS, fl.OTA_CONTROL,
                fl.OTA_DATA, fl.UPDATE_INFO, fl.HARDWARE}
    missing = sorted(f"0x{n:04x}" for n in required - lamp.chars)
    assert not missing


def test_device_info(info):
    assert info.protocol == 1
    assert info.chip in (0, 1)
    assert info.flash_mb >= 4
    assert info.features & 0b1100 == 0b1100, "WiFi and internet update are always supported"
    assert 8 <= info.width <= 128 and 8 <= info.height <= 128
    assert info.width * info.height <= 1024
    assert info.effects_count > 0
    assert info.controls in (0, 1, 2)
    assert re.match(r"^\d+\.\d+\.\d+", info.version)
    assert info.build_date
    assert info.variant
    assert info.previous == "" or re.match(r"^\d+\.\d+\.\d+", info.previous)


def test_device_info_matches_led_settings(lamp, info):
    leds = lamp.read(fl.LED_SETTINGS)
    assert (leds[0], leds[1]) == (info.width, info.height)


def test_mtu(lamp):
    # OTA chunks are MTU - 3 bytes; the lamp accepts up to 517
    assert lamp.mtu >= 23


def test_effects_list(info, effects):
    assert len(effects) == info.effects_count
    codes = [e.code for e in effects]
    assert len(set(codes)) == len(codes), "effect codes are unique"
    for i, e in enumerate(effects):
        assert e.position == i
        assert e.count == info.effects_count
        assert CODE.match(e.code), e.code
        assert e.name
        assert e.flags in (0, 1)
        param_codes = [p.code for p in e.params]
        assert len(set(param_codes)) == len(param_codes), f"{e.code}: parameter codes are unique"
        for p in e.params:
            assert CODE.match(p.code), f"{e.code}.{p.code}"
            assert p.name
            assert p.type in (0, 1)
            assert p.min <= p.default <= p.max, f"{e.code}.{p.code}"
            assert p.min <= p.value <= p.max, f"{e.code}.{p.code}"
            if p.type == 1:
                assert (p.min, p.max) == (0, 1), f"{e.code}.{p.code}: boolean"


def test_known_effects(effects):
    codes = {e.code for e in effects}
    for code in ("fire", "gif", "plasma_waves"):
        assert code in codes


def test_effect_info_by_code(lamp, effects):
    last = effects[-1]
    e = lamp.effect_info(code=last.code)
    assert (e.position, e.code) == (last.position, last.code)


def test_effect_info_rejects_unknown(lamp, effects):
    assert lamp.write_error(fl.EFFECT_INFO, b"\x01" + fl.str8("no_such_effect")) == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.write_error(fl.EFFECT_INFO, bytes([0, len(effects)])) == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.write_error(fl.EFFECT_INFO, b"\x02\x00") == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.write_error(fl.EFFECT_INFO, b"\x00") == fl.ATT_INVALID_LENGTH


def test_current_effect_params(lamp, effects):
    code, values = fl.parse_effect_params(lamp.read(fl.EFFECT_PARAM))
    assert code == lamp.state().effect
    assert values == {p.code: p.value for p in lamp.effect_info(code=code).params}


def test_status_values(lamp):
    r = fl.Reader(lamp.read(fl.WIFI_STATUS))
    state, ip, rssi = r.u8(), r.bytes(4), r.i8()
    r.end()
    assert state in (0, 1, 2, 3)
    if state != 2:
        assert ip == b"\0\0\0\0"

    r = fl.Reader(lamp.read(fl.UPDATE_INFO))
    state, version, checked = r.u8(), r.str8(), r.u32()
    r.end()
    assert state in (0, 1, 2, 3, 4)
    if state == 3:
        assert version
