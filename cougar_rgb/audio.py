"""Анализ звука, который играет в системе: захват монитора выхода по умолчанию через parec.

При смене устройства вывода по умолчанию захват переезжает на новый выход
(следим за событиями сервера через pactl subscribe).
"""
import subprocess
import threading
import time
from dataclasses import dataclass

import numpy as np

RATE = 48000
WINDOW = 2048                # окно FFT (~43 мс) — разрешение ~23 Гц, хватает для баса
HOP = 512                    # новый анализ каждые ~11 мс
BANDS = {'bass': (30, 150), 'mid': (150, 2000), 'treble': (2000, 10000)}
SILENCE_RMS = 1e-4
PEAK_DECAY = 0.5 ** (HOP / RATE / 4.0)      # автоусиление забывает пик примерно за 4 с (полураспад)
BEAT_AVG = HOP / RATE / 0.25                # скорость скользящего среднего баса (~0.25 с)
BEAT_RATIO = 1.35
BEAT_MIN_LEVEL = 0.3
BEAT_COOLDOWN = 0.12


def default_sink():
    try:
        out = subprocess.run(['pactl', 'get-default-sink'], capture_output=True, text=True, timeout=2)
        return out.stdout.strip() or None
    except (OSError, subprocess.SubprocessError):
        return None


@dataclass(frozen=True)
class Features:
    bass: float = 0.0        # 0..1, нормированная громкость полос
    mid: float = 0.0
    treble: float = 0.0
    level: float = 0.0       # общая громкость 0..1
    beats: int = 0           # счётчик ударов баса
    time: float = 0.0


class Analyzer:
    def __init__(self):
        self.features = Features()
        self._stop = threading.Event()
        self._thread = None
        self._proc = None
        self._watcher = None
        self._watch_thread = None
        self._sink = None
        self._switch = threading.Event()
        freqs = np.fft.rfftfreq(WINDOW, 1 / RATE)
        self._masks = {name: (freqs >= lo) & (freqs < hi) for name, (lo, hi) in BANDS.items()}
        self._window = np.hanning(WINDOW).astype(np.float32)

    @property
    def running(self):
        return self._thread is not None and self._thread.is_alive()

    def start(self):
        if self.running:
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, name='audio', daemon=True)
        self._thread.start()
        self._watch_thread = threading.Thread(target=self._watch, name='audio-watch', daemon=True)
        self._watch_thread.start()

    def stop(self):
        self._stop.set()
        for proc in (self._proc, self._watcher):
            if proc:
                proc.terminate()
        for thread in (self._thread, self._watch_thread):
            if thread:
                thread.join(timeout=2)
        self._thread = self._watch_thread = None
        self.features = Features()

    def _run(self):
        while not self._stop.is_set():
            self._switch.clear()
            self._sink = default_sink()
            try:
                self._capture(self._sink + '.monitor' if self._sink else '@DEFAULT_MONITOR@')
            except OSError:
                pass
            if not self._switch.is_set():
                self._stop.wait(1.0)     # parec упал или не запустился — пробуем снова

    def _watch(self):
        """Перезапускает захват, когда меняется выход по умолчанию."""
        while not self._stop.is_set():
            try:
                self._watcher = subprocess.Popen(['pactl', 'subscribe'], stdout=subprocess.PIPE,
                                                 stderr=subprocess.DEVNULL, text=True)
                for line in self._watcher.stdout:
                    if "'change' on server" not in line:
                        continue
                    sink = default_sink()
                    if sink and sink != self._sink:
                        self._switch.set()
                        proc = self._proc
                        if proc:
                            proc.terminate()
                self._watcher.wait()
            except OSError:
                pass
            self._stop.wait(1.0)

    def _capture(self, source):
        self._proc = subprocess.Popen(
            ['parec', '--raw', '-d', source, '--format=float32le', '--rate=%d' % RATE,
             '--channels=1', '--latency-msec=10', '--client-name=cougar-rgb'],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        buf = np.zeros(WINDOW, dtype=np.float32)
        peaks = {name: 1e-3 for name in BANDS}
        peak_level = 1e-3
        bass_avg = 0.0
        beats, last_beat = 0, 0.0
        try:
            while not self._stop.is_set():
                raw = self._proc.stdout.read(HOP * 4)
                if len(raw) < HOP * 4:
                    return
                buf = np.roll(buf, -HOP)
                buf[-HOP:] = np.frombuffer(raw, dtype='<f4')
                now = time.monotonic()

                rms = float(np.sqrt(np.mean(buf[-HOP * 2:] ** 2)))
                if rms < SILENCE_RMS:
                    self.features = Features(beats=beats, time=now)
                    continue

                power = np.abs(np.fft.rfft(buf * self._window)) ** 2
                values = {}
                for name, mask in self._masks.items():
                    amp = float(np.sqrt(power[mask].sum()))
                    peaks[name] = max(amp, peaks[name] * PEAK_DECAY)
                    values[name] = amp / peaks[name]
                peak_level = max(rms, peak_level * PEAK_DECAY)

                bass = values['bass']
                if bass > bass_avg * BEAT_RATIO and bass > BEAT_MIN_LEVEL and now - last_beat > BEAT_COOLDOWN:
                    beats += 1
                    last_beat = now
                bass_avg += (bass - bass_avg) * BEAT_AVG

                self.features = Features(bass=bass, mid=values['mid'], treble=values['treble'],
                                         level=rms / peak_level, beats=beats, time=now)
        finally:
            self._proc.terminate()
            self._proc.wait()
            self._proc = None


analyzer = Analyzer()
