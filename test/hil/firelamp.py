"""
FireLamp test client: the BLE protocol of docs/BLE_PROTOCOL.md (as a phone app speaks it)
and the serial log of the lamp.

The BLE side is a synchronous wrapper over bleak: an asyncio loop runs in a background thread,
so tests are plain functions.
"""

import asyncio
import queue
import re
import struct
import threading
import time
from dataclasses import dataclass, field

from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

try:
    from bleak.exc import BleakGATTProtocolError  # bleak >= 3
except ImportError:  # pragma: no cover
    BleakGATTProtocolError = None

SERVICE = "f1a70000-b390-49c3-8770-a1dfc323d1af"


def uuid(num):
    return f"f1a7{num:04x}-b390-49c3-8770-a1dfc323d1af"


DEVICE_INFO = 0x0001
LAMP_STATE = 0x0002
EFFECT_INFO = 0x0003
EFFECT_PARAM = 0x0004
TIME = 0x0005
TIME_ZONE = 0x0006
SCHEDULE = 0x0007
LED_SETTINGS = 0x0008
WIFI_SETTINGS = 0x0009
WIFI_STATUS = 0x000A
DEVICE_NAME = 0x000B
COMMAND = 0x000C
DEMO_SETTINGS = 0x000D
DEMO_EFFECTS = 0x000E
OTA_CONTROL = 0x0010
OTA_DATA = 0x0011
UPDATE_INFO = 0x0012
HARDWARE = 0x0013
WIFI_NETWORKS = 0x0014

NOTIFYING = (LAMP_STATE, EFFECT_PARAM, WIFI_STATUS, OTA_CONTROL, UPDATE_INFO, WIFI_NETWORKS)

CMD_REBOOT = 0x01
CMD_RESET_EFFECTS = 0x02
CMD_FACTORY_RESET = 0x03
CMD_NEXT_EFFECT = 0x04
CMD_PREV_EFFECT = 0x05
CMD_CHECK_UPDATE = 0x06
CMD_INSTALL_UPDATE = 0x07
CMD_WIFI_SCAN = 0x08

MASK_ON, MASK_EFFECT, MASK_BRIGHTNESS, MASK_FPS, MASK_DEMO = 1, 2, 4, 8, 16

ATT_INVALID_LENGTH = 0x0D
ATT_VALUE_NOT_ALLOWED = 0x13

OTA_START, OTA_END, OTA_ABORT = 1, 2, 3
OTA_IDLE, OTA_RECEIVING, OTA_SUCCESS, OTA_ERROR = 0, 1, 2, 3

SCAN_NONE, SCAN_RUNNING, SCAN_DONE, SCAN_ERROR = 0, 1, 2, 3


################################################################################
# Binary helpers


class Reader:
    def __init__(self, data):
        self.data = bytes(data)
        self.pos = 0

    def u8(self):
        v = self.data[self.pos]
        self.pos += 1
        return v

    def i8(self):
        v = struct.unpack_from("<b", self.data, self.pos)[0]
        self.pos += 1
        return v

    def u16(self):
        v = struct.unpack_from("<H", self.data, self.pos)[0]
        self.pos += 2
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return v

    def str8(self):
        n = self.u8()
        s = self.data[self.pos:self.pos + n]
        if len(s) != n:
            raise ValueError("str8 runs past the end")
        self.pos += n
        return s.decode("utf-8")

    def bytes(self, n):
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b

    def end(self):
        if self.pos != len(self.data):
            raise ValueError(f"{len(self.data) - self.pos} extra bytes")


def str8(s):
    b = s.encode("utf-8") if isinstance(s, str) else bytes(s)
    return bytes([len(b)]) + b


def u16(v):
    return struct.pack("<H", v)


def u32(v):
    return struct.pack("<I", v)


################################################################################
# Decoded values


@dataclass
class DeviceInfo:
    protocol: int
    chip: int
    flash_mb: int
    features: int
    width: int
    height: int
    effects_count: int
    controls: int
    version: str
    build_date: str
    variant: str


@dataclass
class LampState:
    on: int
    brightness: int
    fps: int
    demo: int
    effect: str


@dataclass
class Param:
    code: str
    name: str
    type: int
    min: int
    max: int
    default: int
    value: int


@dataclass
class EffectInfo:
    position: int
    count: int
    code: str
    name: str
    flags: int
    params: list = field(default_factory=list)


@dataclass
class Network:
    ssid: str
    rssi: int
    security: int
    channel: int


def parse_device_info(d):
    r = Reader(d)
    v = DeviceInfo(r.u8(), r.u8(), r.u8(), r.u16(), r.u8(), r.u8(), r.u8(), r.u8(),
                   r.str8(), r.str8(), r.str8())
    r.end()
    return v


def parse_state(d):
    r = Reader(d)
    v = LampState(r.u8(), r.u8(), r.u8(), r.u8(), r.str8())
    r.end()
    return v


def parse_effect_info(d):
    r = Reader(d)
    e = EffectInfo(r.u8(), r.u8(), r.str8(), r.str8(), r.u8())
    for _ in range(r.u8()):
        e.params.append(Param(r.str8(), r.str8(), r.u8(), r.u8(), r.u8(), r.u8(), r.u8()))
    r.end()
    return e


def parse_effect_params(d):
    """Read value of 0x0004: (effect code, {param: value})"""
    r = Reader(d)
    code = r.str8()
    params = {}
    for _ in range(r.u8()):
        p = r.str8()
        params[p] = r.u8()
    r.end()
    return code, params


def parse_param_notify(d):
    r = Reader(d)
    v = (r.str8(), r.str8(), r.u8())
    r.end()
    return v


def parse_ota_status(d):
    r = Reader(d)
    v = (r.u8(), r.u8(), r.u32(), r.u32())
    r.end()
    return v


def parse_networks(d):
    """(state, total, first, [Network])"""
    r = Reader(d)
    state, total, first, n = r.u8(), r.u8(), r.u8(), r.u8()
    nets = [Network(r.str8(), r.i8(), r.u8(), r.u8()) for _ in range(n)]
    r.end()
    return state, total, first, nets


def state_write(mask=0, on=0, brightness=0, fps=0, demo=0, effect=""):
    return bytes([mask, on, brightness, fps, demo]) + str8(effect)


################################################################################
# Errors


class AttError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


_ATT_CODE = re.compile(r"(?:ATT error|error)[^0-9]*0x([0-9a-fA-F]{1,2})|\(0x([0-9a-fA-F]{2})\)")


def att_code(err):
    """ATT error code of a bleak error, None if unknown"""
    if BleakGATTProtocolError and isinstance(err, BleakGATTProtocolError):
        return int(err.code)
    m = _ATT_CODE.search(str(err))
    if not m:
        return None
    return int(m.group(1) or m.group(2), 16)


################################################################################
# BLE client


class Lamp:
    """Connected, paired lamp. All methods are synchronous."""

    def __init__(self, address, timeout=20.0):
        self.address = address
        self.timeout = timeout
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self.loop.run_forever, daemon=True)
        self.thread.start()
        self.client = None
        self.queues = {n: queue.Queue() for n in NOTIFYING}
        self.chars = set()

    def _run(self, coro, timeout=None):
        return asyncio.run_coroutine_threadsafe(coro, self.loop).result(timeout or self.timeout + 10)

    # connection

    def connect(self, pair=True):
        self._run(self._connect(pair), timeout=self.timeout * 3)

    async def _connect(self, pair):
        self.client = BleakClient(self.address, timeout=self.timeout)
        await self.client.connect()
        if pair:
            try:
                await self.client.pair()
            except (BleakError, NotImplementedError):
                # already bonded, or the platform pairs on the first encrypted access
                pass
        svc = self.client.services.get_service(SERVICE)
        if svc is None:
            raise RuntimeError("FireLamp service not found")
        # BlueZ does not report the MTU by itself (bleak assumes 23), ask for it
        backend = getattr(self.client, "_backend", None)
        if hasattr(backend, "_acquire_mtu"):
            try:
                await backend._acquire_mtu()
            except Exception:  # noqa: BLE001 - keep the default MTU
                pass
        self.chars = {int(c.uuid[4:8], 16) for c in svc.characteristics}
        for n in NOTIFYING:
            if n in self.chars:
                await self.client.start_notify(uuid(n), self._make_handler(n))

    def _make_handler(self, num):
        def handler(_, data):
            self.queues[num].put((time.monotonic(), bytes(data)))
        return handler

    def disconnect(self):
        if self.client:
            try:
                self._run(self.client.disconnect())
            except Exception:
                pass

    def reconnect(self, wait_s=60.0):
        """After a reboot: wait until the lamp advertises again and connect"""
        self.disconnect()
        deadline = time.monotonic() + wait_s
        last = None
        while time.monotonic() < deadline:
            try:
                dev = self._run(BleakScanner.find_device_by_address(self.address, timeout=5.0))
                if dev:
                    self.connect(pair=False)
                    return
            except Exception as e:  # noqa: BLE001 - keep trying until the deadline
                last = e
            time.sleep(1)
        raise TimeoutError(f"lamp did not come back in {wait_s} s: {last}")

    @property
    def connected(self):
        return bool(self.client and self.client.is_connected)

    @property
    def mtu(self):
        return self.client.mtu_size

    def has(self, num):
        return num in self.chars

    def close(self):
        self.disconnect()
        self.loop.call_soon_threadsafe(self.loop.stop)

    # raw access

    def read(self, num):
        return bytes(self._run(self.client.read_gatt_char(uuid(num))))

    def write(self, num, data):
        """Write with response; raises AttError with the ATT error code on rejection"""
        try:
            self._run(self.client.write_gatt_char(uuid(num), bytes(data), response=True))
        except BleakError as e:
            raise AttError(att_code(e), str(e)) from e

    def write_error(self, num, data):
        """Write that must be rejected: returns the ATT error code"""
        try:
            self.write(num, data)
        except AttError as e:
            return e.code
        raise AssertionError(f"write {bytes(data).hex()} to 0x{num:04x} was accepted")

    # notifications

    def drain(self, num=None):
        for n in ([num] if num else NOTIFYING):
            q = self.queues[n]
            while not q.empty():
                q.get_nowait()

    def wait_notify(self, num, pred=lambda d: True, timeout=10.0):
        """First notification of `num` matching pred(data); returns the data"""
        deadline = time.monotonic() + timeout
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                raise TimeoutError(f"no matching notification of 0x{num:04x} in {timeout} s")
            try:
                _, data = self.queues[num].get(timeout=left)
            except queue.Empty:
                continue
            if pred(data):
                return data

    def no_notify(self, num, wait=1.0):
        """True if no notification of `num` comes in `wait` seconds"""
        try:
            self.queues[num].get(timeout=wait)
            return False
        except queue.Empty:
            return True

    # decoded helpers

    def device_info(self):
        return parse_device_info(self.read(DEVICE_INFO))

    def state(self):
        return parse_state(self.read(LAMP_STATE))

    def set_state(self, **kw):
        mask = 0
        if "on" in kw:
            mask |= MASK_ON
        if "effect" in kw:
            mask |= MASK_EFFECT
        if "brightness" in kw:
            mask |= MASK_BRIGHTNESS
        if "fps" in kw:
            mask |= MASK_FPS
        if "demo" in kw:
            mask |= MASK_DEMO
        self.write(LAMP_STATE, state_write(mask, **{k: int(v) if k != "effect" else v for k, v in kw.items()}))

    def effect_info(self, position=None, code=None):
        if code is not None:
            self.write(EFFECT_INFO, b"\x01" + str8(code))
        elif position is not None:
            self.write(EFFECT_INFO, bytes([0, position]))
        return parse_effect_info(self.read(EFFECT_INFO))

    def effects(self):
        first = self.effect_info(position=0)
        return [first] + [self.effect_info(position=i) for i in range(1, first.count)]

    def command(self, cmd):
        self.write(COMMAND, bytes([cmd]))

    def networks(self):
        """Whole WiFi networks list, page by page: (state, [Network])"""
        self.write(WIFI_NETWORKS, b"\x00")
        state, total, first, nets = parse_networks(self.read(WIFI_NETWORKS))
        assert first == 0
        result = list(nets)
        while len(result) < total:
            assert nets, "empty page before the end of the list"
            self.write(WIFI_NETWORKS, bytes([len(result)]))
            state, total, first, nets = parse_networks(self.read(WIFI_NETWORKS))
            assert first == len(result)
            result += nets
        return state, result


def find_lamp(name_prefix="FireLamp", timeout=10.0):
    """Address of the first advertising lamp (by the service UUID), None if not found"""
    async def scan():
        devices = await BleakScanner.discover(timeout=timeout, service_uuids=[SERVICE])
        for d in devices:
            if not name_prefix or (d.name or "").startswith(name_prefix):
                return d.address
        return devices[0].address if devices else None
    return asyncio.run(scan())


################################################################################
# Serial log


class SerialLog:
    """Reads the lamp's log in a background thread; can reset the board like esptool does"""

    CRASH = re.compile(r"Guru Meditation|abort\(\) was called|Backtrace:|"
                       r"Task watchdog got triggered|stack overflow|assert failed")

    def __init__(self, port, baud=115200):
        import serial  # pyserial, only needed with --port
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = baud
        self.ser.timeout = 0.2
        # do not reset the board when opening the port (DevKit auto-reset circuit)
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        self.lines = []
        self.lock = threading.Lock()
        self.stop = False
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self):
        buf = b""
        while not self.stop:
            try:
                chunk = self.ser.read(256)
            except Exception:
                break
            if not chunk:
                continue
            buf += chunk
            *done, buf = buf.split(b"\n")
            with self.lock:
                for line in done:
                    text = re.sub(r"\x1b\[[0-9;]*m", "", line.decode("utf-8", "replace")).rstrip("\r")
                    self.lines.append((time.monotonic(), text))

    def mark(self):
        """Position to search from"""
        with self.lock:
            return len(self.lines)

    def since(self, mark=0):
        with self.lock:
            return [t for _, t in self.lines[mark:]]

    def find(self, pattern, mark=0):
        rx = re.compile(pattern)
        return [line for line in self.since(mark) if rx.search(line)]

    def wait_for(self, pattern, mark=0, timeout=30.0):
        rx = re.compile(pattern)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.since(mark):
                m = rx.search(line)
                if m:
                    return m
            time.sleep(0.1)
        raise TimeoutError(f"'{pattern}' not in the log in {timeout} s")

    def crashes(self, mark=0):
        return [line for line in self.since(mark) if self.CRASH.search(line)]

    def reset(self):
        """Hardware reset via EN (RTS), boot from flash (IO0 high)"""
        self.ser.dtr = False
        self.ser.rts = True
        time.sleep(0.2)
        self.ser.rts = False

    def close(self):
        self.stop = True
        self.thread.join(timeout=2)
        self.ser.close()
