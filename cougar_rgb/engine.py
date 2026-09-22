"""Применение эффекта к контроллеру."""
import time

from . import audio
from .device import ALL_ZONES, EFFECT_ZONE, Fusion2
from .effects import EFFECTS, apply_brightness


class Engine:
    def __init__(self, dev: Fusion2):
        self.dev = dev
        self.effect = None
        self.params = None
        self.started = 0.0
        self.last_frame = None

    def setup(self):
        """Первичная настройка после подключения: гасим остальные зоны платы."""
        self.dev.init()
        for zone in ALL_ZONES:
            if zone != EFFECT_ZONE:
                self.dev.set_effect(zone, 1)
        self.dev.apply()

    def apply(self, effect_name, params):
        effect = EFFECTS[effect_name]
        self.effect, self.params = effect, params
        self.started = time.monotonic()
        self.last_frame = None
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
        frame = apply_brightness(self.effect.render(time.monotonic() - self.started, self.params),
                                 self.params.brightness)
        if frame != self.last_frame:
            self.dev.write_leds(frame)
            self.last_frame = frame
