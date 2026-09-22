"""Настройки в ~/.config/cougar-rgb/config.json. Служба перечитывает файл при изменении."""
import json
import os
from pathlib import Path

from .effects import EFFECTS, IDLE_CHOICES, IDLE_DEFAULT, Params

CONFIG_DIR = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config')) / 'cougar-rgb'
CONFIG_PATH = CONFIG_DIR / 'config.json'
DEFAULT_EFFECT = 'sw_rainbow'


def _effect_defaults(effect):
    opts = {'colors': list(effect.default_colors), 'speed': 5, 'reverse': False, 'random': False}
    if effect.audio:
        opts['idle'] = IDLE_DEFAULT
    return opts


class Config:
    def __init__(self, data=None):
        data = data or {}
        self.effect = data.get('effect') if data.get('effect') in EFFECTS else DEFAULT_EFFECT
        self.brightness = int(data.get('brightness', 100))
        self.audio_delays = {k: int(v) for k, v in data.get('audio_delays', {}).items()}
        self.effects = {}
        stored = data.get('effects', {})
        for name, effect in EFFECTS.items():
            opts = _effect_defaults(effect)
            opts.update({k: v for k, v in stored.get(name, {}).items() if k in opts})
            if opts.get('idle', IDLE_DEFAULT) not in IDLE_CHOICES:
                opts['idle'] = IDLE_DEFAULT
            if len(opts['colors']) < effect.colors:
                opts['colors'] += effect.default_colors[len(opts['colors']):]
            self.effects[name] = opts

    @classmethod
    def load(cls, path=CONFIG_PATH):
        try:
            with open(path) as f:
                return cls(json.load(f))
        except (OSError, ValueError):
            return cls()

    def save(self, path=CONFIG_PATH):
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_suffix('.tmp')
        with open(tmp, 'w') as f:
            json.dump({'effect': self.effect, 'brightness': self.brightness, 'audio_delays': self.audio_delays,
                       'effects': self.effects}, f, indent=2, ensure_ascii=False)
        os.replace(tmp, path)

    def params(self, name=None):
        name = name or self.effect
        opts = self.effects[name]
        idle = opts.get('idle')
        return Params(name=name, colors=opts['colors'] or ['#ffffff'], brightness=self.brightness,
                      speed=opts['speed'], reverse=opts['reverse'], random=opts['random'],
                      idle=self.params(idle) if idle in EFFECTS else None)
