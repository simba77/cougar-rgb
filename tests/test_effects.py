import inspect

import pytest

from cougar_rgb.device import LED_COUNT, Fusion2
from cougar_rgb.effects import EFFECTS, IDLE_CHOICES, Params, apply_brightness, parse_color, to_hex


def params_for(effect, **kwargs):
    return Params(colors=effect.default_colors or ['#ffffff'], name=effect.name, **kwargs)


@pytest.mark.parametrize('name', list(EFFECTS))
@pytest.mark.parametrize('speed', [1, 5, 10])
def test_render_produces_valid_frames(name, speed):
    effect = EFFECTS[name]
    p = params_for(effect, speed=speed, random=effect.has_random)
    for t in (0.0, 0.016, 0.5, 3.3, 100.0):
        frame = apply_brightness(effect.render(t, p), 80)
        assert len(frame) == LED_COUNT
        assert all(0 <= c <= 255 for rgb in frame for c in rgb)


@pytest.mark.parametrize('name', [n for n, e in EFFECTS.items() if e.kind == 'hw'])
def test_hw_packet_matches_set_effect_signature(name):
    effect = EFFECTS[name]
    kwargs = effect.hw_packet(params_for(effect))
    inspect.signature(Fusion2.set_effect).bind(None, 5, **kwargs)
    assert 0 <= kwargs.get('max_brightness', 255) <= 255


def test_hw_brightness_uses_full_byte_range():
    static = EFFECTS['static']
    assert static.hw_packet(Params(colors=['#ff0000'], brightness=100))['max_brightness'] == 255
    assert static.hw_packet(Params(colors=['#ff0000'], brightness=0))['max_brightness'] == 0


def test_hw_breathing_speed_matches_rgb_fusion_ranges():
    pulse = EFFECTS['breathing']
    fastest = pulse.hw_packet(Params(colors=['#ffffff'], speed=10))['periods'][0]
    slowest = pulse.hw_packet(Params(colors=['#ffffff'], speed=1))['periods'][0]
    assert (fastest, slowest) == (400, 1600)


def test_apply_brightness_extremes():
    frame = [(255, 128, 1)] * LED_COUNT
    assert apply_brightness(frame, 100) == frame
    assert apply_brightness(frame, 0) == [(0, 0, 0)] * LED_COUNT


def test_color_helpers_roundtrip():
    assert parse_color('#1a2b3c') == (0x1A, 0x2B, 0x3C)
    assert to_hex((0x1A, 0x2B, 0x3C)) == '#1a2b3c'


def test_idle_choices_are_plain_software_effects():
    assert IDLE_CHOICES[0] == 'none'
    for name in IDLE_CHOICES[1:]:
        assert EFFECTS[name].kind == 'sw' and not EFFECTS[name].audio


def test_audio_effects_preview_without_analyzer():
    for effect in (e for e in EFFECTS.values() if e.audio):
        p = params_for(effect)
        frames = [effect.render(i / 60, p) for i in range(120)]
        assert len({tuple(f) for f in frames}) > 1, effect.name
