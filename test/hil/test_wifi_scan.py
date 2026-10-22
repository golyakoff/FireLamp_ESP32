"""WiFi network scan (command 0x08, characteristic 0x0014), firmware >= 0.1.5. Needs --wifi."""

import pytest

import firelamp as fl

pytestmark = pytest.mark.wifi


@pytest.fixture
def scanner(lamp):
    if not lamp.has(fl.WIFI_NETWORKS):
        pytest.skip("no WiFi networks characteristic (firmware < 0.1.5)")
    return lamp


def scan(lamp, timeout=20.0):
    lamp.drain(fl.WIFI_NETWORKS)
    lamp.command(fl.CMD_WIFI_SCAN)
    start = lamp.wait_notify(fl.WIFI_NETWORKS)
    # a repeated command while scanning is accepted and does nothing
    lamp.command(fl.CMD_WIFI_SCAN)
    end = lamp.wait_notify(fl.WIFI_NETWORKS, lambda d: d[0] in (fl.SCAN_DONE, fl.SCAN_ERROR), timeout)
    return start, end


def wifi_mode(lamp):
    return lamp.read(fl.WIFI_SETTINGS)[0]


def test_scan(scanner):
    start, end = scan(scanner)
    assert fl.parse_networks(start) == (fl.SCAN_RUNNING, 0, 0, []), "header only, scanning"
    state, total, first, nets = fl.parse_networks(end)
    assert state == fl.SCAN_DONE, "scan failed (in the always-on mode: the lamp may be connecting, retry)"
    assert (first, nets) == (0, []), "notifications carry the header only"

    state, networks = scanner.networks()
    assert state == fl.SCAN_DONE
    assert len(networks) == total
    assert total > 0, "no networks found at all"

    ssids = [n.ssid for n in networks]
    assert all(ssids), "no hidden networks"
    assert len(set(ssids)) == len(ssids), "unique SSIDs"
    rssi = [n.rssi for n in networks]
    assert rssi == sorted(rssi, reverse=True), "strongest first"
    for n in networks:
        assert -127 <= n.rssi < 0, n
        assert n.security in (0, 1, 2, 3), n
        assert 1 <= n.channel <= 14, f"2.4 GHz channels only: {n}"


def test_scan_paging(scanner):
    _, end = scan(scanner)
    if end[0] != fl.SCAN_DONE:
        pytest.skip("scan failed")
    total = end[1]
    scanner.write(fl.WIFI_NETWORKS, bytes([total]))
    state, t, first, nets = fl.parse_networks(scanner.read(fl.WIFI_NETWORKS))
    assert (state, t, first, nets) == (fl.SCAN_DONE, total, total, [])
    if total < 255:
        assert scanner.write_error(fl.WIFI_NETWORKS, bytes([total + 1])) == fl.ATT_VALUE_NOT_ALLOWED
    assert scanner.write_error(fl.WIFI_NETWORKS, b"") == fl.ATT_INVALID_LENGTH
    # the page size fits one read: 512 bytes
    scanner.write(fl.WIFI_NETWORKS, b"\x00")
    assert len(scanner.read(fl.WIFI_NETWORKS)) <= 512


def test_scan_keeps_wifi_off(scanner, log):
    if wifi_mode(scanner) != 0:
        pytest.skip("WiFi is not off")
    scan(scanner)
    assert scanner.read(fl.WIFI_STATUS)[0] == 0, "WiFi is off after the scan"
    if log:
        assert not log.find(r"WiFi: connecting"), "the scan does not connect"


def test_scan_keeps_connection(scanner):
    if wifi_mode(scanner) != 2 or scanner.read(fl.WIFI_STATUS)[0] != 2:
        pytest.skip("WiFi is not connected in the always-on mode")
    before = scanner.read(fl.WIFI_STATUS)
    _, end = scan(scanner)
    assert end[0] == fl.SCAN_DONE
    after = scanner.read(fl.WIFI_STATUS)
    assert after[:5] == before[:5], "still connected with the same IP"
