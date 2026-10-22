# Hardware-in-the-loop tests

Tests against a real lamp: the PC plays the phone app over BLE (as described in
[docs/BLE_PROTOCOL.md](../../docs/BLE_PROTOCOL.md)) and, optionally, reads the lamp's log over
USB. They check what the host tests cannot: the BLE protocol on the real firmware, settings
storage, reboots and the firmware update.

## Setup

- A PC with Bluetooth LE. Linux with BlueZ is the main target (`bluetoothctl` must see the
  adapter); macOS and Windows work with [bleak](https://github.com/hbldh/bleak) too, but
  pairing and the serial reset are tested on Linux only.
- The lamp with the firmware to test, preferably connected over USB (`--port`).
- Python 3.10+:

```bash
python3 -m venv ~/.venvs/firelamp-hil
~/.venvs/firelamp-hil/bin/pip install -r test/hil/requirements.txt
```

## Running

```bash
cd test/hil
~/.venvs/firelamp-hil/bin/pytest --port /dev/ttyUSB0
```

Close `idf.py monitor` first: the serial port can be opened only once.

Options:

| Option | Meaning |
|--------|---------|
| `--port PORT` | Serial port of the lamp. The lamp is reset before the tests (this opens the pairing window, so the first run on a PC can pair), its log is checked for crashes and unexpected reboots after every test |
| `--lamp ADDRESS` | BLE address of the lamp; default: the first advertising FireLamp |
| `--wifi` | Also run the WiFi scan tests |
| `--reboot` | Also run the tests that reboot the lamp (settings survive a reboot) |
| `--firmware FILE` | Also run the firmware upload tests and **install** `FILE` at the end: abort, SHA-256 mismatch, overflow, disconnect, then the real update. Use the application image: `build-esp32/FireLamp.bin` or a release `*_fw.bin` |

Everything:

```bash
pytest --port /dev/ttyUSB0 --wifi --reboot --firmware ../../build-esp32/FireLamp.bin
```

One test: `pytest test_control.py::test_demo_mode --port /dev/ttyUSB0 -v`.

## Pairing

The lamp accepts bonded phones only, and a new one only in the pairing window (2 minutes after
power on or after 5 clicks). With `--port` the tests reset the lamp first, so the window is open.
Without `--port`, power the lamp on (or make 5 clicks) right before the first run.

The PC takes one of the 4 bond slots of the lamp. To pair again from scratch:
`bluetoothctl remove <address>`.

## What the tests change

The tests restore what they change: lamp state, effect parameters, demo settings and effects,
time zone. Settings are written back with the same values. Exceptions:

- the lamp time is set from the PC;
- `--reboot` reboots the lamp;
- `--firmware` installs the given firmware.

Device name, WiFi settings, LED and hardware settings are never changed.

## Files

- `firelamp.py` - the protocol client (synchronous wrapper over bleak) and the serial log reader;
  usable from a Python shell for manual experiments:

  ```python
  import firelamp as fl
  lamp = fl.Lamp(fl.find_lamp()); lamp.connect()
  lamp.state(); lamp.set_state(brightness=100)
  ```

- `conftest.py` - options and fixtures;
- `test_basics.py` - device info, effects list;
- `test_control.py` - lamp state, parameters, commands, demo mode;
- `test_settings.py` - time, schedule, settings: round trips and validation;
- `test_wifi_scan.py` - WiFi scan (`--wifi`);
- `test_reboot.py` - persistence (`--reboot`);
- `test_ota.py` - firmware upload over BLE (`--firmware`).
