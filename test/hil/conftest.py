"""
Hardware-in-the-loop tests: a real lamp, the PC plays the phone over BLE.
See README.md in this directory.
"""

import time

import pytest

import firelamp as fl


def pytest_addoption(parser):
    g = parser.getgroup("firelamp")
    g.addoption("--lamp", help="BLE address of the lamp (default: the first advertising FireLamp)")
    g.addoption("--port", help="serial port of the lamp, e.g. /dev/ttyUSB0: log checks, reset "
                               "to open the pairing window")
    g.addoption("--firmware", help="firmware .bin for the OTA tests (build-<chip>/FireLamp.bin "
                                   "or a release *_fw.bin)")
    g.addoption("--reboot", action="store_true", help="also run the tests that reboot the lamp")
    g.addoption("--wifi", action="store_true", help="also run the WiFi scan tests (the radio is used)")


def pytest_configure(config):
    for name, text in (("reboot", "reboots the lamp (--reboot)"),
                       ("ota", "uploads firmware over BLE (--firmware)"),
                       ("wifi", "scans WiFi networks (--wifi)")):
        config.addinivalue_line("markers", f"{name}: {text}")


def pytest_collection_modifyitems(config, items):
    skip = {
        "reboot": None if config.getoption("--reboot") else "needs --reboot",
        "ota": None if config.getoption("--firmware") else "needs --firmware",
        "wifi": None if config.getoption("--wifi") else "needs --wifi",
    }
    for item in items:
        for mark, reason in skip.items():
            if reason and mark in item.keywords:
                item.add_marker(pytest.mark.skip(reason=reason))


@pytest.fixture(scope="session")
def log(request):
    """Serial log of the lamp, None without --port"""
    port = request.config.getoption("--port")
    if not port:
        yield None
        return
    s = fl.SerialLog(port)
    yield s
    s.close()


@pytest.fixture(scope="session")
def lamp(request, log):
    address = request.config.getoption("--lamp")
    if log:
        # a reset opens the pairing window, so the first run on this PC can bond
        m = log.mark()
        log.reset()
        log.wait_for(r"BLE: advertising", m, timeout=30)
    if not address:
        address = fl.find_lamp()
        if not address:
            pytest.exit("no advertising FireLamp found; pass --lamp ADDRESS", returncode=2)
    lamp = fl.Lamp(address)
    lamp.connect()
    if log:
        log.wait_for(r"BLE: (bonded phone connected|new phone paired)", timeout=30)
    yield lamp
    lamp.close()


@pytest.fixture(scope="session")
def info(lamp):
    return lamp.device_info()


@pytest.fixture(scope="session")
def effects(lamp):
    return lamp.effects()


@pytest.fixture
def keep_state(lamp):
    """Restore the lamp state (on, effect, brightness, FPS, demo) after the test"""
    s = lamp.state()
    yield s
    lamp.set_state(on=s.on, effect=s.effect, brightness=s.brightness, fps=s.fps, demo=s.demo)
    lamp.drain()


@pytest.fixture(autouse=True)
def no_crash(request, log):
    """Fail a test during which the firmware crashed or rebooted unexpectedly"""
    if not log:
        yield
        return
    m = log.mark()
    yield
    time.sleep(0.2)
    crashes = log.crashes(m)
    assert not crashes, "firmware crash:\n" + "\n".join(crashes)
    if "reboot" not in request.keywords and "ota" not in request.keywords:
        boots = log.find(r"rst:0x|Starting FireLamp", m)
        assert not boots, "unexpected reboot:\n" + "\n".join(boots)
