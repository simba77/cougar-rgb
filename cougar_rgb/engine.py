"""Применение эффекта к контроллеру."""
import time

from . import audio
from .device import ALL_ZONES, EFFECT_ZONE, Fusion2
from .effects import EFFECTS, apply_brightness, mix

IDLE_DELAY = 3.0        # столько секунд тишины до перехода на эффект для тишины
IDLE_FADE_IN = 1.0      # переход в эффект для тишины
IDLE_FADE_OUT = 0.3     # возврат к музыке
STALE_AUDIO = 0.5       # нет свежих данных от анализатора дольше — считаем тишиной


class Engine:
    def __init__(self, dev: Fusion2):
        self.dev = dev
        self.effect = None
        self.params = None
        self.started = 0.0
        self.last_frame = None
        self.last_tick = 0.0
        self.last_sound = float('-inf')
        self.idle_k = 1.0       # 0 — музыкальный эффект, 1 — эффект для тишины

    def setup(self):
        """Первичная настройка после подключения: гасим остальные зоны платы."""
        self.dev.init()
        for zone in ALL_ZONES:
            if zone != EFFECT_ZONE:
                self.dev.set_effect(zone, 1)
        self.dev.apply()

    def apply(self, effect_name, params, audio_delays=None):
        effect = EFFECTS[effect_name]
        self.effect, self.params = effect, params
        self.started = self.last_tick = time.monotonic()
        self.last_frame = None
        self.last_sound = float('-inf')
        self.idle_k = 1.0
        audio.analyzer.delays = dict(audio_delays or {})
        if effect.audio:
            audio.analyzer.start()
        else:
            audio.analyzer.stop()
        if effect.kind == 'hw':
            self.dev.set_direct(False)
            self.dev.set_effect(EFFECT_ZONE, **effect.hw_packet(params))
            self.dev.apply()
        else:
            self.dev.set_direct(True)
            self.tick()

    @property
    def fps(self):
        return self.effect.fps if self.effect else 30

    @property
    def animated(self):
        return self.effect is not None and self.effect.kind == 'sw'

    def tick(self):
        if not self.animated:
            return
        now = time.monotonic()
        dt, self.last_tick = now - self.last_tick, now
        t = now - self.started
        frame = self.effect.render(t, self.params)
        if self.effect.audio:
            frame = self._blend_idle(frame, t, now, dt)
        frame = apply_brightness(frame, self.params.brightness)
        if frame != self.last_frame:
            self.dev.write_leds(frame)
            self.last_frame = frame

    def _blend_idle(self, frame, t, now, dt):
        """Плавно подменяет музыкальный эффект эффектом для тишины, когда ничего не играет."""
        f = audio.analyzer.current()
        if f.active and now - audio.analyzer.delay - f.time < STALE_AUDIO:
            self.last_sound = now
        target = 1.0 if now - self.last_sound > IDLE_DELAY else 0.0
        step = dt / (IDLE_FADE_IN if target > self.idle_k else IDLE_FADE_OUT)
        self.idle_k += max(-step, min(step, target - self.idle_k))
        if self.idle_k <= 0:
            return frame
        idle = self.params.idle
        idle_frame = EFFECTS[idle.name].render(t, idle) if idle else [(0, 0, 0)] * len(frame)
        return [mix(a, b, self.idle_k) for a, b in zip(frame, idle_frame)]
