import struct

import pytest

from cougar_rgb import device
from cougar_rgb.device import EFFECT_ZONE, Fusion2


@pytest.fixture
def dev(monkeypatch):
    sent = []

    def ioctl(fd, request, buf):
        assert request == device.HIDIOCSFEATURE
        assert len(buf) == device.PACKET_SIZE
        sent.append(bytes(buf))

    monkeypatch.setattr(device.fcntl, 'ioctl', ioctl)
    d = Fusion2.__new__(Fusion2)
    d.fd, d.path, d.sent = -1, '/dev/hidraw-test', sent
    return d


def test_command_is_padded_feature_report(dev):
    dev.command(device.CMD_APPLY, 0xFF)
    assert dev.sent == [bytes([0xCC, 0x28, 0xFF]).ljust(64, b'\0')]


@pytest.mark.parametrize('enabled, value', [(True, device.DIRECT_BIT), (False, 0)])
def test_set_direct(dev, enabled, value):
    dev.set_direct(enabled)
    assert dev.sent[0][:3] == bytes([0xCC, 0x32, value])


def test_effect_packet_layout(dev):
    dev.set_effect(EFFECT_ZONE, 2, color=(255, 16, 1), max_brightness=200, min_brightness=3,
                   periods=(1000, 900, 200, 0), params=(7, 1, 2, 0))
    buf = dev.sent[0]
    assert buf[1] == 0x20 + EFFECT_ZONE
    assert struct.unpack_from('<II', buf, 2) == (1 << EFFECT_ZONE, 0)
    assert buf[11:14] == bytes([2, 200, 3])
    assert buf[14:18] == bytes([16, 255, 1, 0])       # GRB + выравнивание
    assert buf[18:22] == bytes(4)
    assert struct.unpack_from('<4H', buf, 22) == (1000, 900, 200, 0)
    assert buf[30:34] == bytes([7, 1, 2, 0])


def test_write_leds_splits_into_19_led_packets(dev):
    colors = [(i, 100 + i, 200) for i in range(25)]
    dev.write_leds(colors)
    assert len(dev.sent) == 2
    first, second = dev.sent
    assert first[1] == device.DIRECT_REG
    assert struct.unpack_from('<HB', first, 2) == (0, 57)
    assert struct.unpack_from('<HB', second, 2) == (57, 18)
    assert first[5:8] == bytes([100, 0, 200])            # GRB
    assert second[5:8] == bytes([119, 19, 200])


def test_find_hidraw_matches_vendor_and_product(tmp_path, monkeypatch):
    for name, hid_id in (('hidraw0', '0003:000009DA:00009090'), ('hidraw2', '0003:0000048D:00005702')):
        (tmp_path / name / 'device').mkdir(parents=True)
        (tmp_path / name / 'device' / 'uevent').write_text('DRIVER=hid-generic\nHID_ID=%s\n' % hid_id)
    monkeypatch.setattr(device.glob, 'glob', lambda pattern: sorted(str(p) for p in tmp_path.iterdir()))
    assert device.find_hidraw() == '/dev/hidraw2'


def test_find_hidraw_raises_when_missing(monkeypatch):
    monkeypatch.setattr(device.glob, 'glob', lambda pattern: [])
    with pytest.raises(device.DeviceNotFound):
        device.find_hidraw()
