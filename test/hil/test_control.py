"""Lamp state, effect parameters, commands, demo mode. Everything is restored afterwards."""

import time

import pytest

import firelamp as fl


def notified_state(lamp, pred, timeout=5.0):
    return fl.parse_state(lamp.wait_notify(fl.LAMP_STATE, lambda d: pred(fl.parse_state(d)), timeout))


def test_brightness(lamp, keep_state):
    target = 40 if keep_state.brightness != 40 else 41
    lamp.drain()
    lamp.set_state(brightness=target)
    assert notified_state(lamp, lambda s: s.brightness == target).brightness == target
    assert lamp.state().brightness == target


def test_select_effect(lamp, effects, keep_state):
    other = next(e.code for e in effects if e.code != keep_state.effect)
    lamp.set_state(demo=1)
    lamp.drain()
    lamp.set_state(effect=other)
    s = notified_state(lamp, lambda s: s.effect == other and not s.demo)
    assert s.effect == other
    assert lamp.state().demo == 0, "selecting an effect turns the demo mode off"


def test_on_off(lamp, keep_state):
    lamp.drain()
    lamp.set_state(on=0)
    assert notified_state(lamp, lambda s: not s.on).on == 0
    lamp.set_state(on=1)
    assert notified_state(lamp, lambda s: s.on).on == 1


def test_fps(lamp, keep_state):
    target = 30 if keep_state.fps != 30 else 31
    lamp.set_state(fps=target)
    assert lamp.state().fps == target


@pytest.mark.parametrize("data", [
    fl.state_write(fl.MASK_BRIGHTNESS, brightness=0),
    fl.state_write(fl.MASK_FPS, fps=0),
    fl.state_write(fl.MASK_FPS, fps=121),
    fl.state_write(fl.MASK_EFFECT, effect="no_such_effect"),
    fl.state_write(fl.MASK_ON, on=2),
    fl.state_write(fl.MASK_DEMO, demo=2),
])
def test_state_rejects_invalid(lamp, keep_state, data):
    assert lamp.write_error(fl.LAMP_STATE, data) == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.state() == keep_state, "nothing changed"


def test_state_rejects_bad_length(lamp, keep_state):
    assert lamp.write_error(fl.LAMP_STATE, b"\x04\x00\x80") == fl.ATT_INVALID_LENGTH


def range_param(effects):
    for e in effects:
        for p in e.params:
            if p.type == 0 and p.max - p.min >= 2:
                return e, p
    pytest.skip("no effect with a range parameter")


def test_effect_param(lamp, effects):
    e, p = range_param(effects)
    orig = lamp.effect_info(code=e.code).params[[q.code for q in e.params].index(p.code)].value
    target = p.min if orig != p.min else p.min + 1
    lamp.drain()
    try:
        lamp.write(fl.EFFECT_PARAM, fl.str8(e.code) + fl.str8(p.code) + bytes([target]))
        d = lamp.wait_notify(fl.EFFECT_PARAM, lambda d: fl.parse_param_notify(d)[:2] == (e.code, p.code))
        assert fl.parse_param_notify(d) == (e.code, p.code, target)
        # the same value again: no notification
        lamp.write(fl.EFFECT_PARAM, fl.str8(e.code) + fl.str8(p.code) + bytes([target]))
        assert lamp.no_notify(fl.EFFECT_PARAM, 1.0)
        info = lamp.effect_info(code=e.code)
        assert next(q.value for q in info.params if q.code == p.code) == target
    finally:
        lamp.write(fl.EFFECT_PARAM, fl.str8(e.code) + fl.str8(p.code) + bytes([orig]))
        lamp.drain()


def test_effect_param_rejects_invalid(lamp, effects):
    e, p = range_param(effects)
    if p.max < 255:
        assert lamp.write_error(fl.EFFECT_PARAM, fl.str8(e.code) + fl.str8(p.code) + bytes([p.max + 1])) \
            == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.write_error(fl.EFFECT_PARAM, fl.str8(e.code) + fl.str8("no_such_param") + b"\x01") \
        == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.write_error(fl.EFFECT_PARAM, fl.str8("no_such_effect") + fl.str8(p.code) + b"\x01") \
        == fl.ATT_VALUE_NOT_ALLOWED


def test_next_prev_effect(lamp, effects, keep_state):
    codes = [e.code for e in effects]
    lamp.set_state(effect=codes[0])
    lamp.command(fl.CMD_NEXT_EFFECT)
    assert lamp.state().effect == codes[1]
    lamp.command(fl.CMD_PREV_EFFECT)
    lamp.command(fl.CMD_PREV_EFFECT)
    assert lamp.state().effect == codes[-1], "previous of the first is the last"


def test_command_rejects_unknown(lamp):
    assert lamp.write_error(fl.COMMAND, b"\xee") == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.write_error(fl.COMMAND, b"\x04\x00") == fl.ATT_INVALID_LENGTH


################################################################################
# Demo mode


@pytest.fixture
def demo_settings(lamp):
    orig = lamp.read(fl.DEMO_SETTINGS)
    yield orig
    lamp.write(fl.DEMO_SETTINGS, orig)


def test_demo_settings(lamp, demo_settings):
    r = fl.Reader(demo_settings)
    order, duration, rnd = r.u8(), r.u16(), r.u16()
    r.end()
    assert order in (0, 1) and 10 <= duration <= 3600 and rnd <= 3600

    new = bytes([1]) + fl.u16(25) + fl.u16(5)
    lamp.write(fl.DEMO_SETTINGS, new)
    assert lamp.read(fl.DEMO_SETTINGS) == new
    for bad in (bytes([2]) + fl.u16(60) + fl.u16(0),
                bytes([0]) + fl.u16(9) + fl.u16(0),
                bytes([0]) + fl.u16(3601) + fl.u16(0),
                bytes([0]) + fl.u16(60) + fl.u16(3601)):
        assert lamp.write_error(fl.DEMO_SETTINGS, bad) == fl.ATT_VALUE_NOT_ALLOWED
    assert lamp.read(fl.DEMO_SETTINGS) == new


def test_demo_mode(lamp, effects, keep_state, demo_settings):
    codes = [e.code for e in effects]
    lamp.write(fl.DEMO_SETTINGS, bytes([0]) + fl.u16(10) + fl.u16(0))
    lamp.set_state(on=1, effect=codes[0], demo=1)
    s = lamp.state()
    assert (s.demo, s.effect) == (1, codes[0]), "the demo starts from the selected effect"

    # next/previous keep the demo running
    lamp.command(fl.CMD_NEXT_EFFECT)
    s = lamp.state()
    assert s.demo == 1 and s.effect != codes[0]

    # the timer switches effects by itself (sequential order, 10 s)
    lamp.drain()
    before = s.effect
    s = notified_state(lamp, lambda s: s.effect != before, timeout=15)
    assert s.demo == 1

    lamp.set_state(demo=0)
    assert lamp.state().demo == 0


def test_demo_effects(lamp, effects):
    included = [e.code for e in effects if e.flags & 1]
    assert included, "at least one effect is included"
    code = included[0]
    try:
        lamp.write(fl.DEMO_EFFECTS, fl.str8(code) + b"\x00")
        assert lamp.effect_info(code=code).flags & 1 == 0
    finally:
        lamp.write(fl.DEMO_EFFECTS, fl.str8(code) + b"\x01")
    assert lamp.effect_info(code=code).flags & 1 == 1


def test_demo_effects_last_one_stays(lamp, effects):
    included = [e.code for e in effects if e.flags & 1]
    excluded = []
    try:
        for code in included[:-1]:
            lamp.write(fl.DEMO_EFFECTS, fl.str8(code) + b"\x00")
            excluded.append(code)
        assert lamp.write_error(fl.DEMO_EFFECTS, fl.str8(included[-1]) + b"\x00") == fl.ATT_VALUE_NOT_ALLOWED
    finally:
        for code in excluded:
            lamp.write(fl.DEMO_EFFECTS, fl.str8(code) + b"\x01")
    assert lamp.write_error(fl.DEMO_EFFECTS, fl.str8("no_such_effect") + b"\x01") == fl.ATT_VALUE_NOT_ALLOWED


def test_notifications_are_coalesced(lamp, keep_state):
    """A brightness slider: many writes, at most 4 state notifications per second"""
    lamp.drain()
    start = time.monotonic()
    for b in range(50, 90, 2):
        lamp.set_state(brightness=b)
    elapsed = time.monotonic() - start
    time.sleep(0.5)
    count = 0
    while not lamp.no_notify(fl.LAMP_STATE, 0.05):
        count += 1
    assert count <= 4 * (elapsed + 0.5) + 2
    assert lamp.state().brightness == 88
