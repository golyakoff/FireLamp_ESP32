"""Settings survive a reboot. Needs --reboot."""

import pytest

import firelamp as fl

pytestmark = pytest.mark.reboot


def reboot(lamp, log):
    m = log.mark() if log else 0
    lamp.command(fl.CMD_REBOOT)
    if log:
        log.wait_for(r"Starting FireLamp", m, timeout=20)
    lamp.reconnect()
    lamp.drain()
    if log:
        assert not log.crashes(m)


def test_reboot_keeps_state(lamp, log, info, effects, keep_state):
    demo_orig = lamp.read(fl.DEMO_SETTINGS)
    code = next(e.code for e in effects if e.code != keep_state.effect)
    brightness = 77 if keep_state.brightness != 77 else 78
    try:
        # a long demo duration, so the demo keeps the effect until the check
        lamp.write(fl.DEMO_SETTINGS, bytes([0]) + fl.u16(3600) + fl.u16(0))
        lamp.set_state(effect=code, demo=1, brightness=brightness)
        reboot(lamp, log)
        s = lamp.state()
        assert (s.effect, s.brightness, s.demo) == (code, brightness, 1)
        assert lamp.device_info() == info
        assert lamp.read(fl.DEMO_SETTINGS) == bytes([0]) + fl.u16(3600) + fl.u16(0)
    finally:
        lamp.write(fl.DEMO_SETTINGS, demo_orig)


def test_reboot_keeps_effect_param(lamp, log, effects):
    e, p = next(((e, p) for e in effects for p in e.params if p.type == 0 and p.max > p.min), (None, None))
    if not e:
        pytest.skip("no range parameter")
    orig = next(q.value for q in lamp.effect_info(code=e.code).params if q.code == p.code)
    target = p.max if orig != p.max else p.min
    try:
        lamp.write(fl.EFFECT_PARAM, fl.str8(e.code) + fl.str8(p.code) + bytes([target]))
        # saved on reboot, before the deferred save delay runs out
        reboot(lamp, log)
        assert next(q.value for q in lamp.effect_info(code=e.code).params if q.code == p.code) == target
    finally:
        lamp.write(fl.EFFECT_PARAM, fl.str8(e.code) + fl.str8(p.code) + bytes([orig]))
