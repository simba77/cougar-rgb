"""Низкоуровневый протокол Gigabyte RGB Fusion 2 USB (ITE 048d:5702, прошивка IT5701).

Все команды — HID feature-репорты 0xCC длиной 64 байта.
Корпус висит на разъёме D_LED1 (Z790 GAMING X AX): эффекты — зона 5,
попиксельные данные — регистр 0x58 и бит 0x01 команды 0x32.
Прошивка не переставляет байты цвета, лента ждёт их в порядке GRB — и в эффектах, и в кадрах.
"""
import fcntl
import glob
import os
import struct

VID, PID = 0x048D, 0x5702
REPORT_ID = 0xCC
PACKET_SIZE = 64

EFFECT_ZONE = 5          # зона встроенных эффектов для D_LED1
DIRECT_BIT = 0x01        # бит команды 0x32: разъём в попиксельном режиме
DIRECT_REG = 0x58        # регистр попиксельных данных
LED_COUNT = 8            # светодиодов на вентилятор (хаб дублирует их на все вентиляторы)
ALL_ZONES = range(8)

CMD_EFFECT = 0x20
CMD_APPLY = 0x28
CMD_BEAT = 0x31
CMD_DIRECT = 0x32
CMD_LED_COUNT = 0x34
CMD_INFO = 0x60


def _grb(rgb):
    r, g, b = (int(c) for c in rgb)
    return bytes((g, r, b))


class DeviceNotFound(OSError):
    pass


def _ioc(nr, size=PACKET_SIZE):
    return (3 << 30) | (size << 16) | (ord('H') << 8) | nr


HIDIOCSFEATURE = _ioc(0x06)
HIDIOCGFEATURE = _ioc(0x07)


def find_hidraw():
    tag = 'HID_ID=0003:%08X:%08X' % (VID, PID)
    for path in sorted(glob.glob('/sys/class/hidraw/hidraw*')):
        with open(path + '/device/uevent') as f:
            if tag in f.read():
                return '/dev/' + os.path.basename(path)
    raise DeviceNotFound('RGB Fusion 2 controller (048d:5702) not found')


class Fusion2:
    def __init__(self):
        self.path = find_hidraw()
        self.fd = os.open(self.path, os.O_RDWR)

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def send(self, packet):
        buf = bytearray(packet).ljust(PACKET_SIZE, b'\0')
        buf[0] = REPORT_ID
        fcntl.ioctl(self.fd, HIDIOCSFEATURE, buf)

    def command(self, *args):
        self.send(bytes((REPORT_ID,) + args))

    def info(self):
        self.command(CMD_INFO)
        buf = bytearray(PACKET_SIZE)
        buf[0] = REPORT_ID
        fcntl.ioctl(self.fd, HIDIOCGFEATURE, buf)
        fw = buf[4:8]
        return {
            'product': buf[1],
            'device_num': buf[2],
            'firmware': '%d.%d.%d.%d' % tuple(fw),
            'name': bytes(buf[12:40]).split(b'\0')[0].decode(errors='replace'),
            'chip_id': '0x%08X' % struct.unpack_from('<I', buf, 56)[0],
        }

    def init(self):
        """Базовая инициализация: без аудио-режима, короткая лента на ARGB-разъёмах."""
        self.command(CMD_BEAT, 0)
        self.command(CMD_LED_COUNT, 0x00, 0x00, 0x00)

    def set_direct(self, enabled):
        self.command(CMD_DIRECT, DIRECT_BIT if enabled else 0)

    def set_effect(self, zone, effect_type, color=(0, 0, 0), max_brightness=255, min_brightness=0,
                   color1=(0, 0, 0), periods=(0, 0, 0, 0), params=(0, 0, 0, 0)):
        """color — (r, g, b); periods — миллисекунды (нарастание, затухание, удержание, ...)."""
        buf = bytearray(PACKET_SIZE)
        buf[1] = CMD_EFFECT + zone
        struct.pack_into('<II', buf, 2, 1 << zone, 0)
        buf[11] = effect_type
        buf[12] = max_brightness
        buf[13] = min_brightness
        struct.pack_into('<3sx3sx4H4B', buf, 14, _grb(color), _grb(color1), *periods, *params)
        self.send(buf)

    def apply(self):
        self.command(CMD_APPLY, 0xFF)

    def write_leds(self, colors):
        """colors — последовательность (r, g, b) 0..255, не больше 19 штук на пакет."""
        offset = 0
        for start in range(0, len(colors), 19):
            chunk = colors[start:start + 19]
            buf = bytearray(PACKET_SIZE)
            buf[1] = DIRECT_REG
            struct.pack_into('<HB', buf, 2, offset, len(chunk) * 3)
            for i, (r, g, b) in enumerate(chunk):
                buf[5 + i * 3:8 + i * 3] = _grb((r, g, b))
            self.send(buf)
            offset += len(chunk) * 3
