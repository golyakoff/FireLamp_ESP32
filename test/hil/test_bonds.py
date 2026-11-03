"""Paired phones: the list, the name the app sends and forgetting (firmware >= 0.11.0)"""

import firelamp as fl


def test_bonded_phones_present(lamp):
    assert lamp.has(fl.BONDED_PHONES), "0x0016 is missing; the firmware is older than 0.11.0"


def test_bonded_phones(lamp):
    capacity, phones = lamp.bonded_phones()
    assert capacity == 5
    assert 1 <= len(phones) <= capacity
    assert sum(p.current for p in phones) == 1, "exactly one entry is this connection"
    for phone in phones:
        assert phone.type in (0, 1, 2, 3)
        assert len(phone.address) == 6
        assert len(phone.name.encode("utf-8")) <= 48


def test_set_bond_name(lamp):
    _, before = lamp.bonded_phones()
    me = next(p for p in before if p.current)

    lamp.set_bond_name("HIL test phone")
    _, after = lamp.bonded_phones()
    assert next(p for p in after if p.current).name == "HIL test phone"

    # put back what was there (an empty name removes it)
    lamp.set_bond_name(me.name)
    _, restored = lamp.bonded_phones()
    assert next(p for p in restored if p.current).name == me.name


def test_set_bond_name_rejects_controls(lamp):
    assert lamp.write_error(fl.COMMAND, bytes([fl.CMD_SET_BOND_NAME]) + fl.str8("a\x01b")) == fl.ATT_VALUE_NOT_ALLOWED


def test_forget_current_phone_rejected(lamp):
    _, phones = lamp.bonded_phones()
    me = next(p for p in phones if p.current)
    assert lamp.write_error(fl.COMMAND, bytes([fl.CMD_FORGET_BOND, me.type]) + me.address) == fl.ATT_VALUE_NOT_ALLOWED


def test_forget_unknown_phone_rejected(lamp):
    assert lamp.write_error(fl.COMMAND, bytes([fl.CMD_FORGET_BOND, 0]) + b"\x01\x02\x03\x04\x05\x06") \
        == fl.ATT_VALUE_NOT_ALLOWED


def test_forget_bond_needs_the_address(lamp):
    assert lamp.write_error(fl.COMMAND, bytes([fl.CMD_FORGET_BOND, 0, 1])) == fl.ATT_INVALID_LENGTH
