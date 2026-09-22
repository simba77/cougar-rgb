"""Фоновая служба: держит эффект применённым и рисует программные анимации."""
import fcntl
import logging
import os
import signal
import time
from pathlib import Path

from .config import CONFIG_PATH, Config
from .device import DeviceNotFound, Fusion2
from .engine import Engine

log = logging.getLogger('cougar-rgb')

LOCK_PATH = Path(os.environ.get('XDG_RUNTIME_DIR', '/tmp')) / 'cougar-rgb.lock'
RECONNECT_DELAY = 2.0
HEALTH_INTERVAL = 5.0
RESUME_SETTLE = 2.0


def is_running():
    try:
        with open(LOCK_PATH, 'a') as f:
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
            fcntl.flock(f, fcntl.LOCK_UN)
            return False
    except BlockingIOError:
        return True


def _config_stamp():
    try:
        return CONFIG_PATH.stat().st_mtime_ns
    except FileNotFoundError:
        return None


def _suspend_gap():
    return time.clock_gettime(time.CLOCK_BOOTTIME) - time.clock_gettime(time.CLOCK_MONOTONIC)


class Daemon:
    def __init__(self):
        self.running = True
        self.dev = None
        self.engine = None
        self.stamp = object()
        self.gap = _suspend_gap()
        self.last_health = 0.0

    def stop(self, *_):
        self.running = False

    def disconnect(self):
        if self.dev:
            self.dev.close()
        self.dev = self.engine = None

    def connect(self):
        try:
            self.dev = Fusion2()
            self.engine = Engine(self.dev)
            self.engine.setup()
            log.info('connected to %s (%s)', self.dev.path, self.dev.info()['name'])
            self.stamp = object()   # принудительно применить конфиг
            return True
        except OSError as e:
            if not isinstance(e, DeviceNotFound):
                log.warning('connect failed: %s', e)
            self.disconnect()
            return False

    def apply_config(self):
        cfg = Config.load()
        self.engine.apply(cfg.effect, cfg.params())
        log.info('applied %s, brightness %d%%', cfg.effect, cfg.brightness)

    def step(self):
        gap = _suspend_gap()
        if gap - self.gap > 1.0:
            log.info('resume from suspend detected, reapplying')
            self.gap = gap
            time.sleep(RESUME_SETTLE)
            self.disconnect()
        self.gap = gap

        if self.dev is None and not self.connect():
            time.sleep(RECONNECT_DELAY)
            return

        stamp = _config_stamp()
        if stamp != self.stamp:
            self.stamp = stamp
            self.apply_config()

        now = time.monotonic()
        if self.engine.animated:
            self.engine.tick()
            time.sleep(1 / Engine.FPS)
        else:
            if now - self.last_health > HEALTH_INTERVAL:
                self.last_health = now
                self.dev.info()     # выбросит OSError, если контроллер пропал
            time.sleep(0.1)

    def run(self):
        signal.signal(signal.SIGTERM, self.stop)
        signal.signal(signal.SIGINT, self.stop)
        while self.running:
            try:
                self.step()
            except OSError as e:
                log.warning('device error: %s, reconnecting', e)
                self.disconnect()
                time.sleep(RECONNECT_DELAY)
        self.disconnect()


def main():
    logging.basicConfig(level=logging.INFO, format='%(levelname)s %(message)s')
    lock = open(LOCK_PATH, 'a')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise SystemExit('cougar-rgb daemon is already running')
    Daemon().run()
