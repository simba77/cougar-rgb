"""Эффекты подсветки.

Аппаратные (kind='hw') исполняет прошивка контроллера — работают без запущенной службы.
Программные (kind='sw') рисуются кадр за кадром службой в попиксельном режиме.
Скорость везде 1..10 (10 — быстрее всего), яркость 0..100 %.
"""
import colorsys
import math
import random
from dataclasses import dataclass, field

from . import audio
from .device import LED_COUNT

HW_STATIC, HW_PULSE, HW_FLASH, HW_CYCLE = 1, 2, 3, 4


def parse_color(value):
    value = value.lstrip('#')
    return int(value[0:2], 16), int(value[2:4], 16), int(value[4:6], 16)


def to_hex(rgb):
    return '#%02x%02x%02x' % tuple(int(c) for c in rgb)


def hsv(h, s=1.0, v=1.0):
    return tuple(c * 255 for c in colorsys.hsv_to_rgb(h % 1.0, s, v))


def mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b, strict=True))


def scale(rgb, k):
    return tuple(c * k for c in rgb)


@dataclass
class Params:
    colors: list
    name: str = ''
    brightness: int = 100
    speed: int = 5
    reverse: bool = False
    random: bool = False
    idle: object = None              # Params эффекта для тишины (только у музыкальных)

    def rgb(self, i):
        return parse_color(self.colors[i % len(self.colors)])


@dataclass
class Effect:
    name: str
    title: str
    kind: str
    colors: int = 0                  # сколько цветов настраивается
    default_colors: list = field(default_factory=list)
    has_speed: bool = True
    has_random: bool = False
    has_direction: bool = False
    speed_title: str = 'Скорость'
    audio: bool = False              # нужен анализ звука (запускается только службой)
    fps: int = 30

    # --- аппаратные ---
    def hw_packet(self, p):
        """Возвращает kwargs для Fusion2.set_effect."""
        raise NotImplementedError

    # --- программные и предпросмотр ---
    def render(self, t, p):
        """Кадр из LED_COUNT цветов (r, g, b) в диапазоне 0..255 до применения яркости."""
        raise NotImplementedError


def _hw_speed(p):
    # 0 — самый быстрый, 9 — самый медленный, как в RGB Fusion
    return 10 - max(1, min(10, p.speed))


def _rate(p, slow, fast):
    """Плавная шкала скорости: speed=1 → slow, speed=10 → fast (геометрически)."""
    k = (max(1, min(10, p.speed)) - 1) / 9
    return slow * (fast / slow) ** k


def _hw_brightness(p):
    return round(max(0, min(100, p.brightness)) * 255 / 100)


# ---------------------------------------------------------------- аппаратные

class HwOff(Effect):
    def hw_packet(self, p):
        return dict(effect_type=HW_STATIC)

    def render(self, t, p):
        return [(0, 0, 0)] * LED_COUNT


class HwStatic(Effect):
    def hw_packet(self, p):
        return dict(effect_type=HW_STATIC, color=p.rgb(0), max_brightness=_hw_brightness(p))

    def render(self, t, p):
        return [p.rgb(0)] * LED_COUNT


class HwPulse(Effect):
    def _period(self, p):
        s = _hw_speed(p)
        return 400 + s * 100 if s <= 6 else 1000 + (s - 6) * 200

    def hw_packet(self, p):
        period = self._period(p)
        return dict(effect_type=HW_PULSE, color=p.rgb(0), max_brightness=_hw_brightness(p),
                    periods=(period, period, 200, 0), params=(7 if p.random else 0, 0, 0, 0))

    def render(self, t, p):
        period = self._period(p) / 1000
        cycle = 2 * period + 0.2
        n, x = divmod(t, cycle)
        k = x / period if x < period else max(0.0, 2 - x / period)
        color = hsv(n / 7) if p.random else p.rgb(0)
        return [scale(color, min(1.0, k))] * LED_COUNT


class HwFlash(Effect):
    count = 1

    def hw_packet(self, p):
        params = (7 if p.random else 0, 1, 2, 0) if self.count == 2 else (7 if p.random else 0, 0, 0, 0)
        return dict(effect_type=HW_FLASH, color=p.rgb(0), max_brightness=_hw_brightness(p),
                    periods=(100, 100, _hw_speed(p) * 200 + 700, 0), params=params)

    def render(self, t, p):
        cycle = (_hw_speed(p) * 200 + 700) / 1000 + 0.2 * self.count
        n, x = divmod(t, cycle)
        on = any(i * 0.4 <= x < i * 0.4 + 0.2 for i in range(self.count))
        color = hsv(n / 7) if p.random else p.rgb(0)
        return [color if on else (0, 0, 0)] * LED_COUNT


class HwDoubleFlash(HwFlash):
    count = 2


class HwCycle(Effect):
    def _period(self, p):
        s = _hw_speed(p)
        return s * 100 + 300 + (1300 * (s - 8) if s > 8 else 0)

    def hw_packet(self, p):
        period = self._period(p)
        return dict(effect_type=HW_CYCLE, max_brightness=_hw_brightness(p),
                    periods=(period, period - 200, 0, 0), params=(7, 0, 0, 0))

    def render(self, t, p):
        step = (2 * self._period(p) - 200) / 1000
        return [hsv(t / step / 7)] * LED_COUNT


# ---------------------------------------------------------------- программные

def _direction(p):
    return -1 if p.reverse else 1


class SwRainbow(Effect):
    def render(self, t, p):
        shift = t * _rate(p, 0.05, 1.5) * _direction(p)
        return [hsv(i / LED_COUNT - shift) for i in range(LED_COUNT)]


class SwSpectrum(Effect):
    def render(self, t, p):
        return [hsv(t * _rate(p, 0.01, 0.5))] * LED_COUNT


class SwBreathing(Effect):
    def render(self, t, p):
        period = 1 / _rate(p, 1 / 8, 1 / 1.2)
        n, x = divmod(t, period)
        k = (1 - math.cos(2 * math.pi * x / period)) / 2
        color = hsv(random.Random(int(n)).random()) if p.random else p.rgb(int(n))
        return [scale(color, k ** 2)] * LED_COUNT


class SwComet(Effect):
    tail = 3.0

    def render(self, t, p):
        head = (t * _rate(p, 0.1, 3.0) * _direction(p) * LED_COUNT) % LED_COUNT
        fg, bg = p.rgb(0), p.rgb(1)
        frame = []
        for i in range(LED_COUNT):
            behind = (head - i) * _direction(p) % LED_COUNT
            k = max(0.0, 1 - behind / self.tail) ** 2
            frame.append(mix(bg, fg, k))
        return frame


class SwGradient(Effect):
    def render(self, t, p):
        shift = t * _rate(p, 0.03, 1.0) * _direction(p)
        a, b = p.rgb(0), p.rgb(1)
        return [mix(a, b, (1 - math.cos(2 * math.pi * (i / LED_COUNT - shift))) / 2)
                for i in range(LED_COUNT)]


class SwFire(Effect):
    def render(self, t, p):
        base = p.rgb(0)
        frame = []
        for i in range(LED_COUNT):
            flicker = (math.sin(t * 7.1 * _rate(p, 0.3, 2.5) + i * 1.7)
                       + math.sin(t * 13.3 * _rate(p, 0.3, 2.5) + i * 4.1)) / 4 + 0.5
            k = 0.35 + 0.65 * flicker
            frame.append((base[0] * k, base[1] * k ** 2, base[2] * k ** 3))
        return frame


class SwRandom(Effect):
    """Каждый диод независимо и плавно переходит в новый случайный цвет."""
    fade = 0.4      # доля периода на переход

    @staticmethod
    def _color(i, n):
        return hsv(random.Random(i * 100003 + n).random())

    def render(self, t, p):
        period = 1 / _rate(p, 1 / 6, 1 / 0.4)
        frame = []
        for i in range(LED_COUNT):
            phase = random.Random(i).random() * period      # диоды меняются не одновременно
            n, x = divmod(t + phase, period)
            n = int(n)
            k = max(0.0, (x / period - (1 - self.fade)) / self.fade)
            frame.append(mix(self._color(i, n), self._color(i, n + 1), k))
        return frame


class AudioEffect(Effect):
    """Эффект под музыку. Без анализатора (в предпросмотре GUI) имитирует ритм 120 BPM."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, audio=True, fps=60, **kwargs)
        self._t = 0.0
        self.reset()

    def _step(self, t):
        dt = t - self._t
        self._t = t
        if not 0 <= dt < 0.2:
            self.reset()
            dt = 0.0
        return dt

    def reset(self):
        pass

    @staticmethod
    def _features(t):
        if audio.analyzer.running:
            return audio.analyzer.current()
        x = t % 0.5
        return audio.Features(bass=math.exp(-x * 8), mid=0.5 + 0.5 * math.sin(t * 0.7),
                              treble=0.5 - 0.5 * math.sin(t * 0.7), level=0.4 + 0.5 * math.exp(-x * 4),
                              beats=int(t * 2))


class SwBassPulse(AudioEffect):
    """Вспышка на ударе баса с затуханием; между ударами подсветка дышит уровнем баса."""

    def reset(self):
        self.env, self.beats, self.n = 0.0, None, 0

    def render(self, t, p):
        dt = self._step(t)
        f = self._features(t)
        release = 1 / _rate(p, 1 / 0.9, 1 / 0.12)
        self.env *= math.exp(-dt / release)
        if f.beats != self.beats:
            if self.beats is not None:
                self.env, self.n = 1.0, self.n + 1
            self.beats = f.beats
        self.env = max(self.env, f.bass ** 2 * 0.6)
        color = hsv(random.Random(self.n).random()) if p.random else p.rgb(0)
        return [scale(color, self.env)] * LED_COUNT


class SwSpectrumColor(AudioEffect):
    """Оттенок по балансу частот: бас — красный, середина — зелёный/голубой, верх — фиолетовый."""

    def reset(self):
        self.hue, self.level = 0.0, 0.0

    def render(self, t, p):
        dt = self._step(t)
        f = self._features(t)
        total = f.bass + f.mid + f.treble
        if total > 1e-3:
            target = 0.75 * (0.5 * f.mid + f.treble) / total
            self.hue += (target - self.hue) * (1 - math.exp(-dt / (1 / _rate(p, 1 / 0.6, 1 / 0.05))))
        self.level = max(f.level, self.level * math.exp(-dt / 0.25))
        return [hsv(self.hue, 1.0, self.level ** 1.5)] * LED_COUNT


class SwCustom(Effect):
    def render(self, t, p):
        return [p.rgb(i) for i in range(LED_COUNT)]


EFFECTS = {e.name: e for e in [
    HwStatic('static', 'Статичный цвет', 'hw', colors=1, default_colors=['#ffffff'], has_speed=False),
    HwPulse('breathing', 'Дыхание', 'hw', colors=1, default_colors=['#00a0ff'], has_random=True),
    HwFlash('flash', 'Вспышки', 'hw', colors=1, default_colors=['#ff0000'], has_random=True),
    HwDoubleFlash('double_flash', 'Двойные вспышки', 'hw', colors=1, default_colors=['#ff0000'], has_random=True),
    HwCycle('color_cycle', 'Смена цветов', 'hw'),
    HwOff('off', 'Выключено', 'hw', has_speed=False),

    SwRainbow('sw_rainbow', 'Вращающаяся радуга', 'sw', has_direction=True),
    SwSpectrum('sw_spectrum', 'Плавный спектр', 'sw'),
    SwBreathing('sw_breathing', 'Плавное дыхание', 'sw', colors=3,
                default_colors=['#ff0040', '#00a0ff', '#40ff00'], has_random=True),
    SwComet('sw_comet', 'Комета', 'sw', colors=2, default_colors=['#00ffff', '#000010'], has_direction=True),
    SwGradient('sw_gradient', 'Вращающийся градиент', 'sw', colors=2,
               default_colors=['#ff00c0', '#0060ff'], has_direction=True),
    SwFire('sw_fire', 'Пламя', 'sw', colors=1, default_colors=['#ff6000']),
    SwRandom('sw_random', 'Случайные цвета', 'sw'),
    SwBassPulse('sw_bass_pulse', 'Пульс по басу', 'sw', colors=1, default_colors=['#ff0030'],
                has_random=True, speed_title='Затухание'),
    SwSpectrumColor('sw_spectrum_color', 'Цвет по спектру', 'sw', speed_title='Реакция'),
    SwCustom('sw_custom', 'Свои цвета по диодам', 'sw', colors=LED_COUNT, has_speed=False,
             default_colors=[to_hex(hsv(i / LED_COUNT)) for i in range(LED_COUNT)]),
]}


IDLE_NONE = 'none'
IDLE_CHOICES = [IDLE_NONE] + [name for name, e in EFFECTS.items() if e.kind == 'sw' and not e.audio]
IDLE_DEFAULT = 'sw_rainbow'


def apply_brightness(frame, brightness):
    # Перцептивная шкала: 50 % на слайдере выглядит как половина яркости
    k = (max(0, min(100, brightness)) / 100) ** 2.2
    return [tuple(max(0, min(255, round(c * k))) for c in rgb) for rgb in frame]
