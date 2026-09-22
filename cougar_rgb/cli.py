"""Командная строка: cougar-rgb <команда>."""
import argparse
import subprocess
import sys
from pathlib import Path

from . import daemon
from .config import Config
from .device import DeviceNotFound, Fusion2
from .effects import EFFECTS
from .engine import Engine

ROOT = Path(__file__).resolve().parent.parent
SERVICE_PATH = Path.home() / '.config/systemd/user/cougar-rgb.service'
DESKTOP_PATH = Path.home() / '.local/share/applications/cougar-rgb.desktop'


def cmd_list(args):
    for kind, title in (('hw', 'Аппаратные (работают без службы)'), ('sw', 'Программные (нужна служба)')):
        print(title + ':')
        for e in EFFECTS.values():
            if e.kind == kind:
                print('  %-14s %s' % (e.name, e.title))


def cmd_info(args):
    with Fusion2() as dev:
        for key, value in dev.info().items():
            print('%-10s %s' % (key, value))
        print('%-10s %s' % ('hidraw', dev.path))
    print('%-10s %s' % ('daemon', 'running' if daemon.is_running() else 'stopped'))


def cmd_set(args):
    cfg = Config.load()
    if args.effect:
        cfg.effect = args.effect
    opts = cfg.effects[cfg.effect]
    if args.color:
        opts['colors'] = ['#' + c.lstrip('#').lower() for c in args.color]
    if args.speed is not None:
        opts['speed'] = args.speed
    if args.reverse is not None:
        opts['reverse'] = args.reverse
    if args.random is not None:
        opts['random'] = args.random
    if args.brightness is not None:
        cfg.brightness = args.brightness
    cfg.save()

    if daemon.is_running():
        return
    if EFFECTS[cfg.effect].kind == 'sw':
        print('Программный эффект сохранён, но служба не запущена: cougar-rgb daemon '
              'или systemctl --user start cougar-rgb', file=sys.stderr)
        return
    with Fusion2() as dev:
        engine = Engine(dev)
        engine.setup()
        engine.apply(cfg.effect, cfg.params())


def cmd_daemon(args):
    daemon.main()


def cmd_gui(args):
    from .gui import main
    main()


def cmd_install(args):
    python = sys.executable
    SERVICE_PATH.parent.mkdir(parents=True, exist_ok=True)
    SERVICE_PATH.write_text(f"""[Unit]
Description=Cougar case ARGB lighting (Gigabyte RGB Fusion 2)
After=graphical-session.target

[Service]
Environment=PYTHONPATH={ROOT}
ExecStart={python} -m cougar_rgb daemon
Restart=on-failure
RestartSec=3

[Install]
WantedBy=default.target
""")
    DESKTOP_PATH.parent.mkdir(parents=True, exist_ok=True)
    DESKTOP_PATH.write_text(f"""[Desktop Entry]
Type=Application
Name=Cougar RGB
Comment=Подсветка корпуса
Exec=env PYTHONPATH={ROOT} {python} -m cougar_rgb gui
Icon=preferences-desktop-color
Categories=Settings;HardwareSettings;
""")
    subprocess.run(['systemctl', '--user', 'daemon-reload'], check=True)
    subprocess.run(['systemctl', '--user', 'enable', '--now', 'cougar-rgb.service'], check=True)
    print('Установлено:\n  %s\n  %s' % (SERVICE_PATH, DESKTOP_PATH))


def cmd_uninstall(args):
    subprocess.run(['systemctl', '--user', 'disable', '--now', 'cougar-rgb.service'])
    for path in (SERVICE_PATH, DESKTOP_PATH):
        path.unlink(missing_ok=True)
    subprocess.run(['systemctl', '--user', 'daemon-reload'])


def main():
    parser = argparse.ArgumentParser(prog='cougar-rgb', description='Подсветка корпуса через RGB Fusion 2')
    sub = parser.add_subparsers(dest='command', required=True)

    sub.add_parser('list', help='список эффектов').set_defaults(func=cmd_list)
    sub.add_parser('info', help='информация о контроллере').set_defaults(func=cmd_info)

    p = sub.add_parser('set', help='выбрать эффект и параметры')
    p.add_argument('effect', nargs='?', choices=list(EFFECTS))
    p.add_argument('-c', '--color', action='append', metavar='RRGGBB', help='цвет (можно несколько раз)')
    p.add_argument('-b', '--brightness', type=int, choices=range(0, 101), metavar='0-100')
    p.add_argument('-s', '--speed', type=int, choices=range(1, 11), metavar='1-10')
    p.add_argument('--reverse', action=argparse.BooleanOptionalAction, default=None, help='обратное направление')
    p.add_argument('--random', action=argparse.BooleanOptionalAction, default=None, help='случайные цвета')
    p.set_defaults(func=cmd_set)

    sub.add_parser('daemon', help='фоновая служба').set_defaults(func=cmd_daemon)
    sub.add_parser('gui', help='графический интерфейс').set_defaults(func=cmd_gui)
    sub.add_parser('install', help='установить systemd-службу и ярлык').set_defaults(func=cmd_install)
    sub.add_parser('uninstall', help='удалить службу и ярлык').set_defaults(func=cmd_uninstall)

    args = parser.parse_args()
    try:
        args.func(args)
    except DeviceNotFound as e:
        sys.exit(str(e))
    except PermissionError:
        sys.exit('Нет доступа к /dev/hidraw*: нужно udev-правило из packaging/60-rgb-fusion2.rules')
