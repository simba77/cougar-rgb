"""Фоновая служба: держит эффект применённым и рисует программные анимации."""
import fcntl
import logging
import os
import signal
import subprocess
import threading
import time
from pathlib import Path

from . import audio
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


SLEEP_MONITOR = ['stdbuf', '-oL', 'gdbus', 'monitor', '--system', '--dest', 'org.freedesktop.login1',
                 '--object-path', '/org/freedesktop/login1']


def is_resume_signal(line):
    return line.rstrip().endswith('.PrepareForSleep (false,)')


class SleepWatcher(threading.Thread):
    """Ловит пробуждение по сигналу logind PrepareForSleep(false).

    Разница CLOCK_BOOTTIME и CLOCK_MONOTONIC не годится как единственный признак:
    при сбитых аппаратных часах ядро почти не учитывает время сна.
    """

    def __init__(self):
        super().__init__(name='sleep-watch', daemon=True)
        self.resumed = threading.Event()
        self._stop = threading.Event()
        self._proc = None

    def run(self):
        while not self._stop.is_set():
            try:
                self._proc = subprocess.Popen(SLEEP_MONITOR, stdout=subprocess.PIPE,
                                              stderr=subprocess.DEVNULL, text=True)
                for line in self._proc.stdout:
                    if is_resume_signal(line):
                        self.resumed.set()
                self._proc.wait()
            except OSError as e:
                log.warning('cannot watch logind sleep signals: %s', e)
            self._stop.wait(5.0)

    def stop(self):
        self._stop.set()
        if self._proc:
            self._proc.terminate()


class Daemon:
    def __init__(self):
        self.running = True
        self.dev = None
        self.engine = None
        self.stamp = object()
        self.gap = _suspend_gap()
        self.last_health = 0.0
        self.sleep_watcher = SleepWatcher()

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
        self.engine.apply(cfg.effect, cfg.params(), cfg.audio_delays)
        log.info('applied %s, brightness %d%%', cfg.effect, cfg.brightness)

    def step(self):
        gap = _suspend_gap()
        if self.sleep_watcher.resumed.is_set() or gap - self.gap > 1.0:
            log.info('resume from suspend detected, reinitializing controller')
            time.sleep(RESUME_SETTLE)
            self.sleep_watcher.resumed.clear()
            self.disconnect()
            gap = _suspend_gap()
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
            time.sleep(1 / self.engine.fps)
        else:
            if now - self.last_health > HEALTH_INTERVAL:
                self.last_health = now
                self.dev.info()     # выбросит OSError, если контроллер пропал
            time.sleep(0.1)

    def run(self):
        signal.signal(signal.SIGTERM, self.stop)
        signal.signal(signal.SIGINT, self.stop)
        self.sleep_watcher.start()
        while self.running:
            try:
                self.step()
            except OSError as e:
                log.warning('device error: %s, reconnecting', e)
                self.disconnect()
                time.sleep(RECONNECT_DELAY)
        self.disconnect()
        self.sleep_watcher.stop()
        audio.analyzer.stop()


def main():
    logging.basicConfig(level=logging.INFO, format='%(levelname)s %(message)s')
    lock = open(LOCK_PATH, 'a')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise SystemExit('cougar-rgb daemon is already running') from None
    Daemon().run()
