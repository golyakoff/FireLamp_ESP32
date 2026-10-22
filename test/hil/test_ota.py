"""Firmware upload over BLE. Needs --firmware (installed at the end: use the build you want
on the lamp afterwards). Tests run in file order: failures first, the real update last."""

import hashlib
import time

import pytest

import firelamp as fl

pytestmark = pytest.mark.ota


@pytest.fixture(scope="module")
def firmware(request):
    with open(request.config.getoption("--firmware"), "rb") as f:
        data = f.read()
    assert data[0] == 0xE9, "not an ESP application image (use the app .bin, not the merged one)"
    # esp_app_desc_t follows the image header (24 bytes) and the first segment header (8 bytes)
    assert int.from_bytes(data[32:36], "little") == 0xABCD5432, "no application description"
    version = data[48:80].split(b"\0")[0].decode()
    return data, version


def status(d):
    return fl.parse_ota_status(d)


def start(lamp, data, sha=None):
    lamp.drain(fl.OTA_CONTROL)
    digest = hashlib.sha256(data).digest() if sha is None else sha
    lamp.write(fl.OTA_CONTROL, bytes([fl.OTA_START]) + fl.u32(len(data)) + digest)
    # the partition is erased first
    st = status(lamp.wait_notify(fl.OTA_CONTROL, lambda d: d[0] in (fl.OTA_RECEIVING, fl.OTA_ERROR), 40))
    assert st[0] == fl.OTA_RECEIVING, f"start failed: {st}"


def send(lamp, data, limit=None):
    chunk = min(lamp.mtu - 3, 514)
    end = len(data) if limit is None else min(limit, len(data))
    for pos in range(0, end, chunk):
        lamp.write(fl.OTA_DATA, data[pos:min(pos + chunk, end)])


def wait_idle(lamp):
    st = status(lamp.wait_notify(fl.OTA_CONTROL, lambda d: d[0] in (fl.OTA_IDLE, fl.OTA_ERROR), 15))
    return st


def test_abort(lamp, info, keep_state, firmware):
    data, _ = firmware
    lamp.set_state(on=1)
    start(lamp, data)
    send(lamp, data, 64 * 1024)
    if lamp.has(fl.WIFI_NETWORKS):
        assert lamp.write_error(fl.COMMAND, bytes([fl.CMD_WIFI_SCAN])) == fl.ATT_VALUE_NOT_ALLOWED, \
            "no WiFi scan during a BLE upload"
    lamp.write(fl.OTA_CONTROL, bytes([fl.OTA_ABORT]))
    assert wait_idle(lamp)[0] == fl.OTA_IDLE
    time.sleep(0.5)
    assert lamp.state().on == 1, "the lamp is back on after an aborted update"
    assert lamp.device_info().version == info.version


def test_sha_mismatch(lamp, info, keep_state, firmware):
    data, _ = firmware
    lamp.set_state(on=1)
    start(lamp, data, sha=bytes(32))
    send(lamp, data)
    lamp.write(fl.OTA_CONTROL, bytes([fl.OTA_END]))
    st = status(lamp.wait_notify(fl.OTA_CONTROL, lambda d: d[0] in (fl.OTA_SUCCESS, fl.OTA_ERROR), 30))
    assert st[:2] == (fl.OTA_ERROR, 5), f"SHA-256 mismatch expected: {st}"
    time.sleep(0.5)
    assert lamp.state().on == 1
    assert lamp.device_info().version == info.version


def test_too_much_data(lamp, keep_state, firmware):
    data, _ = firmware
    small = data[:8192]
    start(lamp, small, sha=b"")
    try:
        send(lamp, data[:8192 + 1024])
    except fl.AttError as e:
        # chunks after the failure are rejected
        assert e.code == fl.ATT_VALUE_NOT_ALLOWED
    st = status(lamp.wait_notify(fl.OTA_CONTROL, lambda d: d[0] == fl.OTA_ERROR, 15))
    assert st[1] == 7, f"overflow expected: {st}"


def test_disconnect_aborts(lamp, log, keep_state, firmware):
    data, _ = firmware
    lamp.set_state(on=1)
    m = log.mark() if log else 0
    start(lamp, data)
    send(lamp, data, 32 * 1024)
    lamp.reconnect()
    if log:
        log.wait_for(r"OTA aborted", m, timeout=10)
    time.sleep(0.5)
    assert lamp.state().on == 1
    # a new upload can start
    start(lamp, data)
    lamp.write(fl.OTA_CONTROL, bytes([fl.OTA_ABORT]))
    assert wait_idle(lamp)[0] == fl.OTA_IDLE


def test_install(lamp, log, keep_state, firmware):
    """The real update; the demo mode must survive it (regression of 0.1.3)"""
    data, version = firmware
    demo_orig = lamp.read(fl.DEMO_SETTINGS)
    try:
        lamp.write(fl.DEMO_SETTINGS, bytes([0]) + fl.u16(3600) + fl.u16(0))
        lamp.set_state(on=1, demo=1)
        m = log.mark() if log else 0
        t0 = time.monotonic()
        start(lamp, data)
        send(lamp, data)
        lamp.write(fl.OTA_CONTROL, bytes([fl.OTA_END]))
        st = status(lamp.wait_notify(fl.OTA_CONTROL, lambda d: d[0] in (fl.OTA_SUCCESS, fl.OTA_ERROR), 30))
        assert st[0] == fl.OTA_SUCCESS, f"update failed: {st}"
        speed = len(data) / (time.monotonic() - t0) / 1024
        print(f"\nuploaded {len(data)} bytes, {speed:.1f} KB/s")
        if log:
            log.wait_for(r"Starting FireLamp", m, timeout=30)
        lamp.reconnect(wait_s=90)
        assert lamp.device_info().version == version
        assert lamp.state().demo == 1, "the demo mode survives the update"
        if log:
            assert not log.crashes(m)
    finally:
        lamp.write(fl.DEMO_SETTINGS, demo_orig)
